#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <3ds.h>

#define CAM_WIDTH   400
#define CAM_HEIGHT  240
#define SCREEN_SIZE (CAM_WIDTH * CAM_HEIGHT * 2)
#define WAIT_TIMEOUT 1000000000ULL

#define HIST_BINS   8
#define CLASS_COUNT 6

#define CLASS_UNKNOWN 0
#define CLASS_HAND    1
#define CLASS_FIST    2
#define CLASS_RED     3
#define CLASS_GREEN   4
#define CLASS_BLUE    5
static void drawRect(u16* fb, int w, int h,
                     int x, int y, int rw, int rh, u16 col);
static void drawString(u16* fb, int fw, int fh,
                       int px, int py, const char* str,
                       u16 col, int scale);


static const char* classNames[CLASS_COUNT] = {
    "unknown ",
    "hand    ",
    "fist    ",
    "red obj ",
    "green   ",
    "blue    "
};

static const u16 classColors[CLASS_COUNT] = {
    0xFFFF, 0xFFE0, 0xFD20, 0xF800, 0x07E0,
};

typedef struct {
    u32 r[HIST_BINS], g[HIST_BINS], b[HIST_BINS], total;
} Histogram;

typedef struct {
    float aspectRatio, density, colorDominance;
} ShapeFeature;

typedef struct {
    float r[HIST_BINS], g[HIST_BINS], b[HIST_BINS];
    float aspectMin, aspectMax;
    float densityMin, densityMax;
    float colorDomMin, colorDomMax;
    float colorWeight;
} ClassTemplate;

static const ClassTemplate templates[CLASS_COUNT] = {
    {0},
    // HAND (calibrated)
    {
        .r={0.00f,0.03f,0.31f,0.21f,0.25f,0.20f,0.00f,0.00f},
        .g={0.00f,0.26f,0.27f,0.03f,0.17f,0.26f,0.00f,0.00f},
        .b={0.00f,0.20f,0.31f,0.04f,0.19f,0.25f,0.00f,0.00f},
        .aspectMin=0.7f,.aspectMax=1.2f,
        .densityMin=0.01f,.densityMax=0.05f,
        .colorDomMin=0.0f,.colorDomMax=0.8f,
        .colorWeight=0.5f
    },
    // FIST (calibrated)
    {
        .r={0.05f,0.25f,0.32f,0.10f,0.05f,0.16f,0.06f,0.00f},
        .g={0.11f,0.43f,0.16f,0.03f,0.04f,0.18f,0.05f,0.00f},
        .b={0.12f,0.45f,0.13f,0.03f,0.03f,0.21f,0.03f,0.00f},
        .aspectMin=0.6f,.aspectMax=2.2f,
        .densityMin=0.01f,.densityMax=0.10f,
        .colorDomMin=0.0f,.colorDomMax=2.5f,
        .colorWeight=0.4f
    },
    // RED
    {
        .r={.01f,.01f,.02f,.04f,.08f,.18f,.32f,.34f},
        .g={.25f,.22f,.20f,.15f,.10f,.05f,.02f,.01f},
        .b={.25f,.22f,.20f,.15f,.10f,.05f,.02f,.01f},
        .aspectMin=0.5f,.aspectMax=2.5f,
        .densityMin=0.40f,.densityMax=0.98f,
        .colorDomMin=0.0f,.colorDomMax=0.5f,
        .colorWeight=0.8f
    },
    // GREEN
    {
        .r={.15f,.15f,.15f,.15f,.15f,.12f,.08f,.05f},
        .g={.02f,.03f,.05f,.10f,.20f,.28f,.22f,.10f},
        .b={.15f,.15f,.15f,.15f,.15f,.12f,.08f,.05f},
        .aspectMin=0.5f,.aspectMax=2.5f,
        .densityMin=0.40f,.densityMax=0.98f,
        .colorDomMin=1.0f,.colorDomMax=1.5f,
        .colorWeight=0.8f
    },
    // BLUE
    {
        .r={.25f,.22f,.20f,.15f,.10f,.05f,.02f,.01f},
        .g={.25f,.22f,.20f,.15f,.10f,.05f,.02f,.01f},
        .b={.01f,.01f,.02f,.04f,.08f,.18f,.32f,.34f},
        .aspectMin=0.5f,.aspectMax=2.5f,
        .densityMin=0.40f,.densityMax=0.98f,
        .colorDomMin=1.5f,.colorDomMax=2.5f,
        .colorWeight=0.8f
    },
};

// PIXEL FONT — 5x7 characters
// Each char: 5 columns, each column is 7 bits
// Chars available: A-Z and space

// 5 wide x 7 tall, stored as 5 bytes (one per column, bit0=top)
static const u8 pixFont[27][5] = {
    // A
    {0x7E,0x09,0x09,0x09,0x7E},
    // B
    {0x7F,0x49,0x49,0x49,0x36},
    // C
    {0x3E,0x41,0x41,0x41,0x22},
    // D
    {0x7F,0x41,0x41,0x22,0x1C},
    // E
    {0x7F,0x49,0x49,0x49,0x41},
    // F
    {0x7F,0x09,0x09,0x09,0x01},
    // G
    {0x3E,0x41,0x49,0x49,0x7A},
    // H
    {0x7F,0x08,0x08,0x08,0x7F},
    // I
    {0x00,0x41,0x7F,0x41,0x00},
    // J
    {0x20,0x40,0x41,0x3F,0x01},
    // K
    {0x7F,0x08,0x14,0x22,0x41},
    // L
    {0x7F,0x40,0x40,0x40,0x40},
    // M
    {0x7F,0x02,0x0C,0x02,0x7F},
    // N
    {0x7F,0x04,0x08,0x10,0x7F},
    // O
    {0x3E,0x41,0x41,0x41,0x3E},
    // P
    {0x7F,0x09,0x09,0x09,0x06},
    // Q
    {0x3E,0x41,0x51,0x21,0x5E},
    // R
    {0x7F,0x09,0x19,0x29,0x46},
    // S
    {0x46,0x49,0x49,0x49,0x31},
    // T
    {0x01,0x01,0x7F,0x01,0x01},
    // U
    {0x3F,0x40,0x40,0x40,0x3F},
    // V
    {0x1F,0x20,0x40,0x20,0x1F},
    // W
    {0x3F,0x40,0x38,0x40,0x3F},
    // X
    {0x63,0x14,0x08,0x14,0x63},
    // Y
    {0x07,0x08,0x70,0x08,0x07},
    // Z
    {0x61,0x51,0x49,0x45,0x43},
    // space (index 26)
    {0x00,0x00,0x00,0x00,0x00},
};

// Draw a single character at pixel position (px, py)
// scale=2 means each dot drawn as 2x2 pixels
static void drawChar(u16* fb, int fw, int fh,
                     int px, int py, char c,
                     u16 col, int scale) {
    int idx;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    else idx = 26;

    for (int col5 = 0; col5 < 5; col5++) {
        u8 colBits = pixFont[idx][col5];
        for (int row7 = 0; row7 < 7; row7++) {
            if (colBits & (1 << row7)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int x = px + col5*scale + sx;
                        int y = py + row7*scale + sy;
                        if (x>=0&&x<fw&&y>=0&&y<fh)
                            fb[y*fw+x] = col;
                    }
                }
            }
        }
    }
}

// Draw a string at (px, py)
static void drawString(u16* fb, int fw, int fh,
                       int px, int py, const char* str,
                       u16 col, int scale) {
    int charW = 5*scale + scale; // 5 cols + 1 spacing
    for (int i = 0; str[i] != '\0'; i++) {
        drawChar(fb, fw, fh, px + i*charW, py,
                 str[i], col, scale);
    }
}

// Draw class badge: colored bar top-left with label
static void drawClassBadge(u16* fb, int classId,
                            bool motionDetected) {
    u16 bgCol  = classColors[classId];
    u16 txtCol = 0x0000; // black text on colored bg

    // Special case: unknown (dark bg, white text)
    if (classId == CLASS_UNKNOWN) {
        bgCol  = 0x4208; // dark gray
        txtCol = 0xFFFF;
    }

    // Badge background: top left, 120px wide x 20px tall
    int badgeW = 130;
    int badgeH = 20;

    // Only draw badge if motion detected
    if (motionDetected) {
        // Colored badge
        drawRect(fb, CAM_WIDTH, CAM_HEIGHT,
                 4, 4, badgeW, badgeH, bgCol);

        // Label — trim trailing spaces
        char label[16];
        strncpy(label, classNames[classId], 15);
        label[15] = '\0';
        // Remove trailing spaces
        for (int i = strlen(label)-1; i >= 0; i--) {
            if (label[i] == ' ') label[i] = '\0';
            else break;
        }

        // Draw text at scale 1 (5x7 per char, fits in 20px height)
        drawString(fb, CAM_WIDTH, CAM_HEIGHT,
                   10, 7, label, txtCol, 2);
    } else {
        // No motion — small gray indicator
        drawRect(fb, CAM_WIDTH, CAM_HEIGHT,
                 4, 4, badgeW, badgeH, 0x2104);
        drawString(fb, CAM_WIDTH, CAM_HEIGHT,
                   10, 7, "no motion", 0x8410, 1);
    }
}

// =============================================
// CLASSIFICATION
// =============================================

static void extractHistogram(u16* frame, int fw, int fh,
                              int x, int y, int w, int h,
                              Histogram* hist) {
    memset(hist, 0, sizeof(Histogram));
    for (int row=y; row<y+h; row++) {
        if (row<0||row>=fh) continue;
        for (int col=x; col<x+w; col++) {
            if (col<0||col>=fw) continue;
            u16 px=frame[row*fw+col];
            u8 r5=(px>>11)&0x1F;
            u8 g6=(px>>5) &0x3F;
            u8 b5= px     &0x1F;
            hist->r[r5*HIST_BINS/32]++;
            hist->g[g6*HIST_BINS/64]++;
            hist->b[b5*HIST_BINS/32]++;
            hist->total++;
        }
    }
}

static ShapeFeature extractShape(u16* curr, u16* prev,
                                  int fw, int fh,
                                  int cx, int cy,
                                  int boxW, int boxH,
                                  int thresh) {
    ShapeFeature sf={1.0f,0.5f,0.0f};
    int x=cx-boxW/2, y=cy-boxH/2;
    int minX=fw,maxX=0,minY=fh,maxY=0,mc=0;
    long rSum=0,gSum=0,bSum=0;
    for (int row=y; row<y+boxH; row++) {
        if (row<0||row>=fh) continue;
        for (int col=x; col<x+boxW; col++) {
            if (col<0||col>=fw) continue;
            int i=row*fw+col;
            int cg=(curr[i]>>5)&0x3F;
            int pg=(prev[i]>>5)&0x3F;
            int diff=cg-pg; if(diff<0)diff=-diff;
            if (diff>thresh) {
                if(col<minX)minX=col; if(col>maxX)maxX=col;
                if(row<minY)minY=row; if(row>maxY)maxY=row;
                mc++;
                u16 px=curr[i];
                rSum+=(px>>11)&0x1F;
                gSum+=(px>>5) &0x3F;
                bSum+= px     &0x1F;
            }
        }
    }
    if (mc>0&&maxX>minX&&maxY>minY) {
        float bw=(float)(maxX-minX), bh=(float)(maxY-minY);
        sf.aspectRatio=(bh>0)?bw/bh:1.0f;
        sf.density=(bw*bh>0)?(float)mc/(bw*bh):0.0f;
        float avgR=(float)rSum/mc;
        float avgG=(float)(gSum/2)/mc;
        float avgB=(float)bSum/mc;
        if(avgR>=avgG&&avgR>=avgB) sf.colorDominance=0.0f;
        else if(avgG>=avgR&&avgG>=avgB) sf.colorDominance=1.0f;
        else sf.colorDominance=2.0f;
    }
    return sf;
}

static float colorDistance(Histogram* hist,
                           const ClassTemplate* tmpl) {
    if(hist->total==0) return 999.0f;
    float dist=0.0f;
    for(int i=0;i<HIST_BINS;i++){
        float rn=(float)hist->r[i]/hist->total;
        float gn=(float)hist->g[i]/hist->total;
        float bn=(float)hist->b[i]/hist->total;
        float dr=rn-tmpl->r[i];
        float dg=gn-tmpl->g[i];
        float db=bn-tmpl->b[i];
        dist+=dr*dr+dg*dg+db*db;
    }
    return dist;
}

static float shapeDistance(ShapeFeature* sf,
                           const ClassTemplate* tmpl) {
    float dist=0.0f;
    if(sf->aspectRatio<tmpl->aspectMin)
        dist+=(tmpl->aspectMin-sf->aspectRatio)*2.0f;
    else if(sf->aspectRatio>tmpl->aspectMax)
        dist+=(sf->aspectRatio-tmpl->aspectMax)*2.0f;
    if(sf->density<tmpl->densityMin)
        dist+=(tmpl->densityMin-sf->density)*3.0f;
    else if(sf->density>tmpl->densityMax)
        dist+=(sf->density-tmpl->densityMax)*3.0f;
    float dd=sf->colorDominance-tmpl->colorDomMin;
    if(dd<0)dd=-dd;
    if(dd>0.5f) dist+=dd;
    return dist;
}

static int classify(u16* curr, u16* prev,
                    int fw, int fh,
                    int cx, int cy, int thresh,
                    Histogram* histOut,
                    ShapeFeature* sfOut) {
    int boxW=80, boxH=80;
    int x=cx-boxW/2, y=cy-boxH/2;
    Histogram hist;
    extractHistogram(curr,fw,fh,x,y,boxW,boxH,&hist);
    ShapeFeature sf=extractShape(curr,prev,fw,fh,
                                  cx,cy,boxW,boxH,thresh);
    if(histOut) *histOut=hist;
    if(sfOut)   *sfOut=sf;
    float bestScore=999.0f;
    int   bestClass=CLASS_UNKNOWN;
    for(int i=1;i<CLASS_COUNT;i++){
        float cw=templates[i].colorWeight;
        float sw=1.0f-cw;
        float cd=colorDistance(&hist,&templates[i]);
        float sd=shapeDistance(&sf,  &templates[i]);
        float total=cw*cd+sw*sd;
        if(total<bestScore){bestScore=total;bestClass=i;}
    }
    return (bestScore<0.25f)?bestClass:CLASS_UNKNOWN;
}

// =============================================
// CALIBRATION
// =============================================

static const char* calibLabels[CLASS_COUNT] = {
    "unknown", "hand", "fist", "red", "green", "blue"
};
static int  calibTargetClass = CLASS_HAND;
static char saveStatusMsg[64] = "";

static void saveCalibration(Histogram* hist,
                             ShapeFeature* sf,
                             int classId) {
    if (!hist || hist->total == 0) {
        snprintf(saveStatusMsg,64,"No data to save!");
        return;
    }
    FILE* f = fopen("/3ds/calibration.txt", "a");
    if (!f) {
        snprintf(saveStatusMsg,64,"File open failed!");
        return;
    }
    fprintf(f, "=== %s ===\n", calibLabels[classId]);
    fprintf(f, ".r={");
    for (int i=0;i<HIST_BINS;i++)
        fprintf(f,"%.2ff%s",(float)hist->r[i]/hist->total,
                i<HIST_BINS-1?",":"");
    fprintf(f, "},\n.g={");
    for (int i=0;i<HIST_BINS;i++)
        fprintf(f,"%.2ff%s",(float)hist->g[i]/hist->total,
                i<HIST_BINS-1?",":"");
    fprintf(f, "},\n.b={");
    for (int i=0;i<HIST_BINS;i++)
        fprintf(f,"%.2ff%s",(float)hist->b[i]/hist->total,
                i<HIST_BINS-1?",":"");
    fprintf(f, "},\n");
    fprintf(f,"// aspect=%.2f density=%.2f coldom=%.1f\n",
            sf->aspectRatio,sf->density,sf->colorDominance);
    fprintf(f,"// .aspectMin=%.1ff,.aspectMax=%.1ff,\n",
            sf->aspectRatio*0.7f,sf->aspectRatio*1.3f);
    fprintf(f,"// .densityMin=%.2ff,.densityMax=%.2ff,\n\n",
            sf->density*0.6f,sf->density*1.4f);
    fclose(f);
    snprintf(saveStatusMsg,64,"Saved %s!",
             calibLabels[classId]);
}

static void printCalibration(Histogram* hist,
                              ShapeFeature* sf) {
    printf("\x1b[2J");
    printf("=== CALIBRATION ===\n");
    printf("X=exit UP/DN=class Y=save\n");
    printf("Target: [%s]\n\n",
           calibLabels[calibTargetClass]);
    if (!hist || hist->total == 0) {
        printf("Move object first\n");
        if (saveStatusMsg[0])
            printf("\n%s\n", saveStatusMsg);
        return;
    }
    printf("R: ");
    for(int i=0;i<HIST_BINS;i++)
        printf("%.2f ",(float)hist->r[i]/hist->total);
    printf("\nG: ");
    for(int i=0;i<HIST_BINS;i++)
        printf("%.2f ",(float)hist->g[i]/hist->total);
    printf("\nB: ");
    for(int i=0;i<HIST_BINS;i++)
        printf("%.2f ",(float)hist->b[i]/hist->total);
    printf("\nAspect: %.2f  Density: %.2f\n",
           sf->aspectRatio, sf->density);
    printf("ColDom: %.1f\n", sf->colorDominance);
    if (saveStatusMsg[0])
        printf("\n>>> %s\n", saveStatusMsg);
    else
        printf("\nY = save as [%s]\n",
               calibLabels[calibTargetClass]);
}

// =============================================
// MOTION DETECTION
// =============================================

typedef struct {
    bool motion; int pixels, cx, cy;
} MotionResult;

static MotionResult detectMotion(u16* curr, u16* prev,
                                  int w, int h, int thresh) {
    MotionResult r={false,0,0,0};
    long sx=0,sy=0,n=0;
    for(int y=0;y<h;y++)
        for(int x=0;x<w;x++){
            int i=y*w+x;
            int cg=(curr[i]>>5)&0x3F;
            int pg=(prev[i]>>5)&0x3F;
            int diff=cg-pg; if(diff<0)diff=-diff;
            if(diff>thresh){sx+=x;sy+=y;n++;}
        }
    r.pixels=(int)n; r.motion=(n>200);
    if(r.motion){r.cx=(int)(sx/n);r.cy=(int)(sy/n);}
    return r;
}

// =============================================
// SIMULATION
// =============================================

static void simulateFrame(u16* frame, int bx, int by) {
    for(int i=0;i<CAM_WIDTH*CAM_HEIGHT;i++) frame[i]=0x2104;
    int rad=30;
    for(int y=by-rad;y<=by+rad;y++)
        for(int x=bx-rad;x<=bx+rad;x++){
            if(x<0||x>=CAM_WIDTH||y<0||y>=CAM_HEIGHT) continue;
            int dx=x-bx,dy=y-by;
            if(dx*dx+dy*dy<=rad*rad)
                frame[y*CAM_WIDTH+x]=0xFFFF;
        }
}

// =============================================
// DRAWING
// =============================================

static void drawRect(u16* fb,int w,int h,
                     int x,int y,int rw,int rh,u16 col){
    for(int row=y;row<y+rh;row++){
        if(row<0||row>=h) continue;
        for(int c=x;c<x+rw;c++){
            if(c<0||c>=w) continue;
            fb[row*w+c]=col;
        }
    }
}

static void drawCrosshair(u16* fb,int w,int h,
                           int cx,int cy,u16 col){
    drawRect(fb,w,h,cx-20,cy-2,40,4,col);
    drawRect(fb,w,h,cx-2,cy-20,4,40,col);
    drawRect(fb,w,h,cx-5,cy-5,10,10,col);
}

static void drawBudgetBar(u16* fb,int w,int pct){
    int barW=(w*pct)/100; if(barW>w)barW=w;
    u16 col=(pct<80)?0x07E0:(pct<100)?0xFFE0:0xF800;
    drawRect(fb,w,CAM_HEIGHT,0,CAM_HEIGHT-6,barW,6,col);
    drawRect(fb,w,CAM_HEIGHT,barW,CAM_HEIGHT-6,w-barW,6,0x39E7);
}

static void writeFbRGB565(void* fb,void* img,
                           u16 x,u16 y,u16 w,u16 h){
    u8* fb8=(u8*)fb; u16* img16=(u16*)img;
    for(int j=0;j<h;j++)
        for(int i=0;i<w;i++){
            int dy=y+h-j,dx=x+i;
            u32 v=(dy+dx*h)*3;
            u16 d=img16[j*w+i];
            fb8[v]  =((d>>11)&0x1F)<<3;
            fb8[v+1]=((d>>5) &0x3F)<<2;
            fb8[v+2]=( d     &0x1F)<<3;
        }
}

// =============================================
// CAMERA MANAGEMENT
// =============================================

#define MODE_INNER 0
#define MODE_OUTER 1
#define MODE_SIM   2
#define MODE_COUNT 3

static const char* modeNames[MODE_COUNT]={
    "INNER CAM","OUTER CAM","SIMULATED"
};

static void stopCamera(Handle* events,u32 port){
    CAMU_StopCapture(port); CAMU_ClearBuffer(port);
    for(int i=0;i<2;i++)
        if(events[i]){svcCloseHandle(events[i]);events[i]=0;}
    CAMU_Activate(SELECT_NONE);
}

static void startCamera(Handle* events,u32* transferUnit,
                        int mode){
    u32 sel =(mode==MODE_INNER)?SELECT_IN1 :SELECT_OUT2;
    u32 port=(mode==MODE_INNER)?PORT_CAM1  :PORT_CAM2;
    CAMU_SetSize(sel,SIZE_CTR_TOP_LCD,CONTEXT_A);
    CAMU_SetOutputFormat(sel,OUTPUT_RGB_565,CONTEXT_A);
    CAMU_SetFrameRate(sel,FRAME_RATE_30);
    CAMU_SetNoiseFilter(sel,true);
    CAMU_SetAutoExposure(sel,true);
    CAMU_SetAutoWhiteBalance(sel,true);
    CAMU_SetTrimming(port,false);
    CAMU_GetMaxBytes(transferUnit,CAM_WIDTH,CAM_HEIGHT);
    CAMU_SetTransferBytes(port,*transferUnit,CAM_WIDTH,CAM_HEIGHT);
    CAMU_Activate(sel);
    CAMU_GetBufferErrorInterruptEvent(&events[0],port);
    CAMU_ClearBuffer(port);
    CAMU_StartCapture(port);
}

static u32 currentPort(int mode){
    return (mode==MODE_INNER)?PORT_CAM1:PORT_CAM2;
}
static u32 currentSelect(int mode){
    return (mode==MODE_INNER)?SELECT_IN1:SELECT_OUT2;
}

// =============================================
// MAIN
// =============================================

int main(void){
    acInit();
    gfxInitDefault();
    consoleInit(GFX_BOTTOM,NULL);
    gfxSetDoubleBuffering(GFX_TOP,true);
    gfxSetDoubleBuffering(GFX_BOTTOM,false);

    printf("CameraProject 3DS\n");
    printf("=================\n");
    printf("A    = cycle source\n");
    printf("UP/DN= sensitivity\n");
    printf("L/R  = framerate\n");
    printf("X    = calibrate\n");
    printf("START= exit\n\n");

    camInit();

    u16* camBuf =(u16*)malloc(SCREEN_SIZE);
    u16* prevBuf=(u16*)malloc(SCREEN_SIZE);
    u16* dispBuf=(u16*)malloc(SCREEN_SIZE);
    if(!camBuf||!prevBuf||!dispBuf){
        printf("malloc failed\n");
        svcSleepThread(3000000000LL);
        gfxExit(); return 1;
    }
    memset(camBuf, 0,SCREEN_SIZE);
    memset(prevBuf,0,SCREEN_SIZE);
    memset(dispBuf,0,SCREEN_SIZE);

    Handle camEvent[2]={0,0};
    u32    transferUnit=0;
    int    mode=MODE_INNER;
    bool   switchMode=false;
    startCamera(camEvent,&transferUnit,mode);

    bool  captureInterrupted=false;
    bool  firstFrame=true;
    bool  calibMode=false;
    int   thresh=8;
    u32   frames=0,motionFrames=0;
    float smoothX=CAM_WIDTH/2.0f,smoothY=CAM_HEIGHT/2.0f;
    int   lastClass=CLASS_UNKNOWN;
    bool  lastMotion=false;

    Histogram    lastHist; memset(&lastHist,0,sizeof(lastHist));
    ShapeFeature lastSF;   memset(&lastSF,  0,sizeof(lastSF));

    float simX=CAM_WIDTH/2.0f,simY=CAM_HEIGHT/2.0f;
    float simSpeedX=0.0f,simSpeedY=0.0f;

    u64 minT=UINT64_MAX,maxT=0,totalT=0;
    u64 totalDetectT=0,totalClassT=0;

    const u32 frates[]={
        FRAME_RATE_5,FRAME_RATE_10,
        FRAME_RATE_15,FRAME_RATE_30
    };
    const char* frateNames[]={"5","10","15","30"};
    int frateIdx=3;

    while(aptMainLoop()){
        hidScanInput();
        u32 keys=hidKeysDown();
        if(keys&KEY_START) break;

        if(keys&KEY_X){
            calibMode=!calibMode;
            saveStatusMsg[0]='\0';
            if(!calibMode){
                printf("\x1b[2J");
                printf("CameraProject 3DS\n");
                printf("=================\n");
                printf("A    = cycle source\n");
                printf("UP/DN= sensitivity\n");
                printf("L/R  = framerate\n");
                printf("X    = calibrate\n");
                printf("START= exit\n\n");
            }
        }

        if(calibMode){
            if(keys&KEY_UP){
                calibTargetClass=(calibTargetClass+1)%CLASS_COUNT;
                if(calibTargetClass==CLASS_UNKNOWN)
                    calibTargetClass=CLASS_HAND;
                saveStatusMsg[0]='\0';
            }
            if(keys&KEY_DOWN){
                calibTargetClass--;
                if(calibTargetClass<CLASS_HAND)
                    calibTargetClass=CLASS_COUNT-1;
                saveStatusMsg[0]='\0';
            }
            if(keys&KEY_Y)
                saveCalibration(&lastHist,&lastSF,
                                calibTargetClass);
        } else {
            if(keys&KEY_UP)  {if(thresh<30)thresh++;}
            if(keys&KEY_DOWN){if(thresh>1) thresh--;}
            if(keys&KEY_R){
                frateIdx=(frateIdx+1)%4;
                if(mode!=MODE_SIM)
                    CAMU_SetFrameRate(currentSelect(mode),
                                      frates[frateIdx]);
            }
            if(keys&KEY_L){
                frateIdx=(frateIdx+3)%4;
                if(mode!=MODE_SIM)
                    CAMU_SetFrameRate(currentSelect(mode),
                                      frates[frateIdx]);
            }
            if(keys&KEY_A) switchMode=true;
        }

        circlePosition circle;
        hidCircleRead(&circle);

        if(switchMode){
            if(mode!=MODE_SIM)
                stopCamera(camEvent,currentPort(mode));
            mode=(mode+1)%MODE_COUNT;
            if(mode!=MODE_SIM)
                startCamera(camEvent,&transferUnit,mode);
            simX=CAM_WIDTH/2.0f; simY=CAM_HEIGHT/2.0f;
            simSpeedX=simSpeedY=0.0f;
            memset(prevBuf,0,SCREEN_SIZE);
            firstFrame=true; switchMode=false;
            captureInterrupted=false;
            lastClass=CLASS_UNKNOWN; lastMotion=false;
            continue;
        }

        // ---- SIMULATION MODE ----
        if(mode==MODE_SIM){
            u64 t0=osGetTime();
            float dz=15.0f,sp=3.0f;
            if(circle.dx>dz||circle.dx<-dz)
                simSpeedX= (circle.dx/150.0f)*sp;
            else simSpeedX*=0.8f;
            if(circle.dy>dz||circle.dy<-dz)
                simSpeedY=(-circle.dy/150.0f)*sp;
            else simSpeedY*=0.8f;
            simX+=simSpeedX; simY+=simSpeedY;
            int rad=30;
            if(simX<rad){simX=rad;simSpeedX=0;}
            if(simX>CAM_WIDTH-rad){simX=CAM_WIDTH-rad;simSpeedX=0;}
            if(simY<rad){simY=rad;simSpeedY=0;}
            if(simY>CAM_HEIGHT-rad){simY=CAM_HEIGHT-rad;simSpeedY=0;}
            simulateFrame(camBuf,(int)simX,(int)simY);
            memcpy(dispBuf,camBuf,SCREEN_SIZE);
            MotionResult motion={false,0,0,0};
            if(!firstFrame){
                u64 ds=osGetTime();
                motion=detectMotion(camBuf,prevBuf,
                                    CAM_WIDTH,CAM_HEIGHT,thresh);
                totalDetectT+=osGetTime()-ds;
                if(motion.motion){
                    motionFrames++;
                    u64 cs=osGetTime();
                    lastClass=classify(camBuf,prevBuf,
                                       CAM_WIDTH,CAM_HEIGHT,
                                       motion.cx,motion.cy,
                                       thresh,&lastHist,&lastSF);
                    totalClassT+=osGetTime()-cs;
                }
            }
            lastMotion=motion.motion;
            if(motion.motion){
                smoothX=smoothX*0.6f+motion.cx*0.4f;
                smoothY=smoothY*0.6f+motion.cy*0.4f;
                int sx=(int)smoothX,sy=(int)smoothY;
                u16 col=classColors[lastClass];
                drawCrosshair(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx,sy,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy-50,100,3,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy+47,100,3,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy-50,3,100,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx+47,sy-50,3,100,col);
            }
            // Draw class badge top-left
            drawClassBadge(dispBuf, lastClass, lastMotion);
            u64 elapsed=osGetTime()-t0;
            drawBudgetBar(dispBuf,CAM_WIDTH,(int)(elapsed*100/33));
            writeFbRGB565(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,
                          NULL,NULL),dispBuf,0,0,CAM_WIDTH,CAM_HEIGHT);
            gfxFlushBuffers(); gspWaitForVBlank(); gfxSwapBuffers();
            memcpy(prevBuf,camBuf,SCREEN_SIZE);
            firstFrame=false; frames++;
            elapsed=osGetTime()-t0; totalT+=elapsed;
            if(elapsed<minT)minT=elapsed;
            if(elapsed>maxT)maxT=elapsed;
            goto print_stats;
        }

        // ---- CAMERA MODES ----
        if(camEvent[1]==0)
            CAMU_SetReceiving(&camEvent[1],camBuf,
                              currentPort(mode),
                              SCREEN_SIZE,(s16)transferUnit);
        if(captureInterrupted){
            CAMU_StartCapture(currentPort(mode));
            captureInterrupted=false;
        }
        {
            s32 index=-1;
            svcWaitSynchronizationN(&index,camEvent,2,
                                    false,WAIT_TIMEOUT);
            u64 frameStart=osGetTime();
            switch(index){
                case 0:
                    svcCloseHandle(camEvent[1]);
                    camEvent[1]=0; captureInterrupted=true;
                    continue;
                case 1:
                    svcCloseHandle(camEvent[1]);
                    camEvent[1]=0; break;
                default: continue;
            }
            frames++;
            MotionResult motion={false,0,0,0};
            if(!firstFrame){
                u64 ds=osGetTime();
                motion=detectMotion(camBuf,prevBuf,
                                    CAM_WIDTH,CAM_HEIGHT,thresh);
                totalDetectT+=osGetTime()-ds;
                if(motion.motion){
                    motionFrames++;
                    u64 cs=osGetTime();
                    lastClass=classify(camBuf,prevBuf,
                                       CAM_WIDTH,CAM_HEIGHT,
                                       motion.cx,motion.cy,
                                       thresh,&lastHist,&lastSF);
                    totalClassT+=osGetTime()-cs;
                }
            }
            lastMotion=motion.motion;
            memcpy(dispBuf,camBuf,SCREEN_SIZE);
            if(motion.motion){
                smoothX=smoothX*0.6f+motion.cx*0.4f;
                smoothY=smoothY*0.6f+motion.cy*0.4f;
                int sx=(int)smoothX,sy=(int)smoothY;
                u16 col=classColors[lastClass];
                drawCrosshair(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx,sy,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy-50,100,3,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy+47,100,3,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx-50,sy-50,3,100,col);
                drawRect(dispBuf,CAM_WIDTH,CAM_HEIGHT,sx+47,sy-50,3,100,col);
            }
            // Draw class badge top-left
            drawClassBadge(dispBuf, lastClass, lastMotion);
            u64 elapsed=osGetTime()-frameStart;
            drawBudgetBar(dispBuf,CAM_WIDTH,(int)(elapsed*100/33));
            writeFbRGB565(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,
                          NULL,NULL),dispBuf,0,0,CAM_WIDTH,CAM_HEIGHT);
            gfxFlushBuffers(); gspWaitForVBlank(); gfxSwapBuffers();
            memcpy(prevBuf,camBuf,SCREEN_SIZE);
            firstFrame=false;
            elapsed=osGetTime()-frameStart; totalT+=elapsed;
            if(elapsed<minT)minT=elapsed;
            if(elapsed>maxT)maxT=elapsed;
        }

        print_stats:
        if(calibMode){
            printCalibration(&lastHist,&lastSF);
        } else {
            printf("\x1b[6;0H");
            printf("Mode:  %-12s\n",modeNames[mode]);
            printf("Class: %s\n",classNames[lastClass]);
            printf("FPS:   %s fps      \n",frateNames[frateIdx]);
            printf("Fr:    %-6lu      \n",frames);
            printf("Mot:   %-6lu(%.0f%%)\n",
                   motionFrames,
                   frames?(float)motionFrames*100/frames:0.0f);
            printf("Thr:   %-3d        \n",thresh);
            printf("--timing (ms)--    \n");
            printf("Total: %-4llu      \n",
                   frames?totalT/frames:0ULL);
            printf("Det:   %-4llu      \n",
                   frames?totalDetectT/frames:0ULL);
            printf("Cls:   %-4llu      \n",
                   motionFrames?totalClassT/motionFrames:0ULL);
            printf("Min:   %-4llu      \n",
                   minT==UINT64_MAX?0ULL:minT);
            printf("Max:   %-4llu      \n",maxT);
            printf("X=calib           \n");
        }
    }

    if(mode!=MODE_SIM)
        stopCamera(camEvent,currentPort(mode));
    free(camBuf); free(prevBuf); free(dispBuf);
    camExit(); gfxExit(); acExit();
    return 0;
}
