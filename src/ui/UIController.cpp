// src/ui/UIController.cpp — custom-drawn HUD + Control Panel
#include "UIController.h"
#include "core/ImageProcessor.h"
#include "camera/CameraManager.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <ctime>
#include <cmath>
#include <chrono>
#include <algorithm>

UIController::UIController()
    : show_ui(true), show_binary(false), round_status(0),
      is_recognition_allowed(false), current_fps(0.0) {}

UIController::~UIController() {
    try { cv::destroyWindow("Control Panel"); } catch (...) {}
}

// ═══════════════════════════════════════════════════════════════════
//  STATIC CACHE
// ═══════════════════════════════════════════════════════════════════

void UIController::rebuildStaticCache(cv::Size sz) {
    int w = sz.width, h = sz.height, cx = w/2, cy = h/2;
    cv::Mat vig(h, w, CV_8UC3, cv::Scalar(255,255,255));
    int mw = (int)(w*0.55), mh = (int)(h*0.55);
    cv::Mat black(h, w, CV_8UC1, cv::Scalar(0));
    cv::rectangle(black, cv::Rect((w-mw)/2,(h-mh)/2,mw,mh), cv::Scalar(80), -1);
    cv::GaussianBlur(black, black, cv::Size(0,0), std::min(w,h)*0.25);
    cv::Mat v3; cv::cvtColor(black, v3, cv::COLOR_GRAY2BGR);
    vig = cv::Scalar(255,255,255) - v3*0.30;

    cv::Mat scan = cv::Mat::zeros(h, w, CV_8UC3);
    for (int y=2; y<h; y+=3) cv::line(scan, cv::Point(0,y), cv::Point(w,y), cv::Scalar(4,4,4), 1);

    cv::Mat xhair(h, w, CV_8UC4, cv::Scalar(0,0,0,0));
    auto L4=[&](cv::Point a,cv::Point b,cv::Scalar c,int t){cv::line(xhair,a,b,c,t,cv::LINE_AA);};
    auto C4=[&](cv::Point ctr,int r,cv::Scalar col,int t){
        cv::circle(xhair,ctr,r,col,std::abs(t),cv::LINE_AA);
        if(t<0) cv::circle(xhair,ctr,r,col,-1,cv::LINE_AA);
    };
    cv::Scalar Rg(200,200,200,120), Gn(0,255,180,200), Tk(180,180,180,80);
    C4(cv::Point(cx,cy),80,Rg,1); C4(cv::Point(cx,cy),78,cv::Scalar(0,0,0,100),1);
    C4(cv::Point(cx,cy),28,Rg,1);
    float ta[]={0,45,90,135,180,225,270,315}, tl[]={16,8,16,8,16,8,16,8};
    for(int i=0;i<8;i++){float r=ta[i]*CV_PI/180.0f;cv::Point2f o(80*cos(r),80*sin(r)),n2((80-tl[i])*cos(r),(80-tl[i])*sin(r));L4(cv::Point(cx,cy)+cv::Point(o),cv::Point(cx,cy)+cv::Point(n2),Tk,1);}
    int g=10,l=38;
    L4(cv::Point(cx-l,cy),cv::Point(cx-g,cy),Gn,1);L4(cv::Point(cx+g,cy),cv::Point(cx+l,cy),Gn,1);
    L4(cv::Point(cx,cy-l),cv::Point(cx,cy-g),Gn,1);L4(cv::Point(cx,cy+g),cv::Point(cx,cy+l),Gn,1);
    C4(cv::Point(cx,cy),2,Gn,-1);

    static_overlay = vig.clone();
    cv::subtract(static_overlay, scan, static_overlay);
    for(int y=0;y<h;y++){cv::Vec4b*X=xhair.ptr<cv::Vec4b>(y);cv::Vec3b*O=static_overlay.ptr<cv::Vec3b>(y);
        for(int x=0;x<w;x++){float a=X[x][3]/255.0f;if(a>0.001f)for(int ch=0;ch<3;ch++)O[x][ch]=(uchar)(O[x][ch]*(1-a)+X[x][ch]*a);}}
    cached_size=sz;
}

// ═══════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════

cv::Scalar UIController::getArmorColor(const std::string& color) const {
    if(color=="red") return cv::Scalar(0,69,255);
    if(color=="blue")return cv::Scalar(255,140,0);
    return cv::Scalar(0,255,128);
}
void UIController::glowText(cv::Mat& img,const std::string& txt,cv::Point p,double s,const cv::Scalar& c,int t){
    cv::putText(img,txt,p,cv::FONT_HERSHEY_SIMPLEX,s,cv::Scalar(0,0,0),t+2,cv::LINE_AA);
    cv::putText(img,txt,p,cv::FONT_HERSHEY_SIMPLEX,s,c,t,cv::LINE_AA);
}
void UIController::drawCornerBrackets(cv::Mat& img,const cv::Rect& r,const cv::Scalar& c,int len,int t){
    int x=r.x,y=r.y,w=r.width,h=r.height;
    cv::line(img,cv::Point(x,y+len),cv::Point(x,y),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x,y),cv::Point(x+len,y),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x+w,y),cv::Point(x+w-len,y),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x+w,y),cv::Point(x+w,y+len),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x,y+h),cv::Point(x,y+h-len),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x,y+h),cv::Point(x+len,y+h),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x+w,y+h),cv::Point(x+w-len,y+h),c,t,cv::LINE_AA);
    cv::line(img,cv::Point(x+w,y+h),cv::Point(x+w,y+h-len),c,t,cv::LINE_AA);
}

// ═══════════════════════════════════════════════════════════════════
//  HUD LAYERS
// ═══════════════════════════════════════════════════════════════════

void UIController::drawHUD(cv::Mat& image, int armor_count) {
    int bh=40, w=image.cols;
    for(int y=0;y<bh;y++){uchar v=(uchar)(20*(1.0f-(float)y/bh));cv::line(image,cv::Point(0,y),cv::Point(w,y),cv::Scalar(v,v,v),1);}
    cv::line(image,cv::Point(0,bh-1),cv::Point(w,bh-1),cv::Scalar(0,180,180),1,cv::LINE_AA);
    double s=0.5;int yt=28;
    glowText(image,"TGT "+std::to_string(armor_count),cv::Point(14,yt),s,armor_count>0?cv::Scalar(0,255,200):cv::Scalar(120,120,120),2);
    int x=140;cv::line(image,cv::Point(x,17),cv::Point(x,23),cv::Scalar(80,80,80),1,cv::LINE_AA);
    const char* rl[]={"IDLE","R1","R2","DONE"};
    cv::Scalar rc=round_status>0?(round_status==3?cv::Scalar(180,180,180):cv::Scalar(0,255,100)):cv::Scalar(120,120,120);
    glowText(image,rl[round_status],cv::Point(x+14,yt),s,rc,2);
    x=220;cv::line(image,cv::Point(x,17),cv::Point(x,23),cv::Scalar(80,80,80),1,cv::LINE_AA);
    cv::Scalar ac=is_recognition_allowed?cv::Scalar(0,255,80):cv::Scalar(255,80,60);
    glowText(image,is_recognition_allowed?"ARMED":"SAFE",cv::Point(x+14,yt),s,ac,2);
    x=320; cv::line(image,cv::Point(x,17),cv::Point(x,23),cv::Scalar(80,80,80),1,cv::LINE_AA);
    if (m_recording) {
        // pulsing red dot
        static int pulse=0; pulse++;
        int r = (pulse/30)%2 ? 6 : 4;
        cv::circle(image, cv::Point(x+20, yt-8), r, cv::Scalar(0,0,255), -1, cv::LINE_AA);
        glowText(image,"REC",cv::Point(x+32,yt),s,cv::Scalar(0,0,255),2);
    }
    auto now=std::chrono::system_clock::now();std::time_t t=std::chrono::system_clock::to_time_t(now);
    char buf[16];std::strftime(buf,sizeof(buf),"%H:%M:%S",std::localtime(&t));
    glowText(image,buf,cv::Point(w-100,yt),s,cv::Scalar(200,200,200),2);
    std::string fs=std::to_string((int)current_fps);
    cv::Scalar fc=current_fps>=60?cv::Scalar(0,255,100):current_fps>=30?cv::Scalar(0,255,255):cv::Scalar(0,100,255);
    glowText(image,fs,cv::Point(w-170,yt),s,fc,2);
}

void UIController::drawInfoCard(cv::Mat& image,const ArmorInfo& armor,const cv::Scalar& color,const cv::Point& pos,int card_w){
    double sf=0.42;int lh=17,pad=8,stripe=4,card_h=pad*2+4*lh;
    cv::Rect card(pos.x,pos.y,card_w,card_h);
    cv::Rect cl=card&cv::Rect(0,40,image.cols-1,image.rows-64);
    if(cl.width<card_w||cl.height<card_h)return;
    cv::Mat roi=image(cl);roi=roi*0.28+cv::Scalar(6,7,10);
    cv::rectangle(image,cv::Rect(pos.x,pos.y,stripe,card_h),color,-1,cv::LINE_AA);
    cv::rectangle(image,card,cv::Scalar(60,60,70),1,cv::LINE_AA);
    int tx=pos.x+pad+stripe;char buf[64];
    snprintf(buf,sizeof(buf),"%s  %.0f%%",armor.color.c_str(),armor.confidence*100.0f);
    glowText(image,buf,cv::Point(tx,pos.y+lh),sf,cv::Scalar(220,220,230),1);
    int bx=tx,by=pos.y+lh+4,bw=card_w-pad*2-stripe,bh2=4;
    cv::rectangle(image,cv::Rect(bx,by,bw,bh2),cv::Scalar(40,40,50),-1,cv::LINE_AA);
    int fw=(int)(bw*std::min(1.0f,armor.confidence));
    if(fw>0){cv::Scalar bc=armor.confidence>0.7f?cv::Scalar(0,255,120):armor.confidence>0.4f?cv::Scalar(0,220,255):cv::Scalar(255,180,0);cv::rectangle(image,cv::Rect(bx,by,fw,bh2),bc,-1,cv::LINE_AA);}
    snprintf(buf,sizeof(buf),"X:%d  Y:%d",(int)armor.center.x,(int)armor.center.y);
    cv::putText(image,buf,cv::Point(tx,pos.y+lh*2+6),cv::FONT_HERSHEY_SIMPLEX,sf,cv::Scalar(190,190,200),1,cv::LINE_AA);
    snprintf(buf,sizeof(buf),"W:%d  H:%d",armor.bbox.width,armor.bbox.height);
    cv::putText(image,buf,cv::Point(tx,pos.y+lh*3+6),cv::FONT_HERSHEY_SIMPLEX,sf,cv::Scalar(190,190,200),1,cv::LINE_AA);
    int dx=(int)(armor.center.x-image.cols/2.0f),dy=(int)(armor.center.y-image.rows/2.0f);
    snprintf(buf,sizeof(buf),"OFF  %+d  %+d",dx,dy);
    cv::putText(image,buf,cv::Point(tx,pos.y+lh*4+6),cv::FONT_HERSHEY_SIMPLEX,sf,cv::Scalar(160,160,170),1,cv::LINE_AA);
}

void UIController::drawArmorOverlay(cv::Mat& image,const ArmorInfo& armor){
    cv::Scalar color=getArmorColor(armor.color),dim=color*0.55;cv::Rect r=armor.bbox;
    int bl=std::min(24,std::min(r.width,r.height)/3);drawCornerBrackets(image,r,color,bl,2);
    cv::Point ct((int)armor.center.x,(int)armor.center.y);
    cv::circle(image,ct,7,color,1,cv::LINE_AA);cv::line(image,cv::Point(ct.x-5,ct.y),cv::Point(ct.x+5,ct.y),color,1,cv::LINE_AA);cv::line(image,cv::Point(ct.x,ct.y-5),cv::Point(ct.x,ct.y+5),color,1,cv::LINE_AA);
    cv::Point ctr(image.cols/2,image.rows/2);float dist=cv::norm(ct-ctr);
    if(dist>30){int segs=(int)(dist/12);for(int i=0;i<segs;i+=2){float t1=(float)i/segs,t2=std::min(1.0f,(float)(i+1)/segs);cv::Point p1(ct.x+(ctr.x-ct.x)*t1,ct.y+(ctr.y-ct.y)*t1),p2(ct.x+(ctr.x-ct.x)*t2,ct.y+(ctr.y-ct.y)*t2);cv::line(image,p1,p2,dim,1,cv::LINE_AA);}}
    for(const auto& lr:armor.lightRects){cv::Point2f pts[4];lr.points(pts);for(int i=0;i<4;i++)cv::line(image,pts[i],pts[(i+1)%4],dim,1,cv::LINE_AA);}
    int cw=170;cv::Point cp;
    if(r.x+r.width+cw+14<image.cols)cp=cv::Point(r.x+r.width+14,r.y);
    else if(r.x-cw-14>0)cp=cv::Point(r.x-cw-14,r.y);else cp=cv::Point(14,r.y+r.height+8);
    drawInfoCard(image,armor,color,cp,cw);
}

void UIController::drawBottomBar(cv::Mat& image){
    int bh=26,w=image.cols,h=image.rows;
    cv::rectangle(image,cv::Rect(0,h-bh,w,bh),cv::Scalar(10,12,16),-1);
    cv::line(image,cv::Point(0,h-bh-1),cv::Point(w,h-bh-1),cv::Scalar(0,140,140),1,cv::LINE_AA);
    double sf=0.40;int y=h-7;cv::Scalar c(160,160,170);
    cv::putText(image,"^Q:QUIT",cv::Point(12,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"S:UI",cv::Point(110,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"B:BIN",cv::Point(175,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"R:NEW",cv::Point(245,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"A:ARM",cv::Point(315,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"F:FIN",cv::Point(385,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);cv::putText(image,"1-3:EXP",cv::Point(450,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);
    cv::putText(image,"V:REC",cv::Point(545,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);
    cv::putText(image,"SPC:SHT",cv::Point(615,y),cv::FONT_HERSHEY_SIMPLEX,sf,c,1,cv::LINE_AA);
}

// ═══════════════════════════════════════════════════════════════════
//  MAIN DRAW
// ═══════════════════════════════════════════════════════════════════

void UIController::drawUI(cv::Mat& image,const std::vector<ArmorInfo>& armors,
                          ImageProcessor*,CameraManager*){
    if(!show_ui)return;
    cv::Size cur(image.cols,image.rows);
    if(cur!=cached_size)rebuildStaticCache(cur);
    cv::multiply(image,static_overlay,image,1.0/255.0);
    drawHUD(image,(int)armors.size());
    for(const auto& a:armors)drawArmorOverlay(image,a);
    drawToasts(image);
    drawBottomBar(image);
    drawControlPanel();
}

// ═══════════════════════════════════════════════════════════════════
//  CUSTOM CONTROL PANEL
// ═══════════════════════════════════════════════════════════════════

static const int SEC_H  = 30;  // section header height
static const int ROW_H  = 28;  // slider row height
static const int PAD    = 8;

void UIController::updatePanelLayout() {
    // called after sliders/sections are populated
    int total_h = 40; // header
    for (auto& sec : m_sections) {
        total_h += SEC_H;
        if (!sec.collapsed)
            total_h += (int)sec.slider_ids.size() * ROW_H + PAD;
    }
    total_h += 12; // footer padding
    if (total_h > m_panel_h) m_panel_h = total_h;
}

void UIController::createControlPanel(ImageProcessor* processor, CameraManager* camera) {
    // ── populate sliders ──
    m_sliders.clear(); m_sections.clear();

    auto addSlider = [&](const char* name, int* val, int lo, int hi, const char* hint) {
        Slider s; s.name = name; s.val = val; s.lo = lo; s.hi = hi; s.hint = hint;
        m_sliders.push_back(s);
    };

    // CAMERA section
    int cam_start = (int)m_sliders.size();
    addSlider("Exposure", &camera->exposure, 0, 2000, "");
    addSlider("Gain",     &camera->gain,     0,  100, "");
    m_sections.emplace_back(); auto& s0 = m_sections.back();
    s0.name = "CAMERA"; s0.slider_ids = {cam_start, cam_start+1}; s0.collapsed = false;

    // RED ARMOR
    int red_start = (int)m_sliders.size();
    addSlider("H Width",  &processor->red1.h_hi,     0,  90, "");
    addSlider("S/V Min",  &processor->red1.s_lo,0, 255, "");
    m_sections.emplace_back(); auto& s1 = m_sections.back();
    s1.name = "RED ARMOR"; s1.slider_ids = {red_start, red_start+1}; s1.collapsed = false;

    // BLUE ARMOR
    int blue_start = (int)m_sliders.size();
    addSlider("H Center", &processor->blue_range.h_lo,       0, 180, "");
    addSlider("H Width",  &processor->blue_range.h_hi,      0, 180, "");
    addSlider("S/V Min",  &processor->blue_range.s_lo,0, 255, "");
    m_sections.emplace_back(); auto& s2 = m_sections.back();
    s2.name = "BLUE ARMOR"; s2.slider_ids = {blue_start, blue_start+2}; s2.collapsed = false;

    // FILTER
    int flt_start = (int)m_sliders.size();
    addSlider("Min Area",   &processor->filter.min_area,   0,   500, "");
    addSlider("Max Area",   &processor->filter.max_area,   0, 20000, "");
    addSlider("Max Angle",  reinterpret_cast<int*>(&processor->filter.max_angle_diff), 0, 90, "deg");
    m_sections.emplace_back(); auto& s3 = m_sections.back();
    s3.name = "FILTER"; s3.slider_ids = {flt_start, flt_start+2}; s3.collapsed = false;

    // ADVANCED
    int adv_start = (int)m_sliders.size();
    addSlider("Min Ratio",  reinterpret_cast<int*>(&processor->filter.min_ratio), 0, 100, "");
    addSlider("Max Ratio",  reinterpret_cast<int*>(&processor->filter.max_ratio), 0, 100, "");
    addSlider("Min Dist",   &processor->filter.min_distance,          0,  200, "px");
    addSlider("Max Dist",   &processor->filter.max_distance,          0, 1000, "px");
    addSlider("Max H Diff", reinterpret_cast<int*>(&processor->filter.max_height_diff_ratio), 0, 100, "%");
    m_sections.emplace_back(); auto& s4 = m_sections.back();
    s4.name = "ADVANCED"; s4.slider_ids = {adv_start, adv_start+4}; s4.collapsed = true;

    m_panel_w = 380;
    m_panel_h = 640;
    updatePanelLayout();

    cv::namedWindow("Control Panel", cv::WINDOW_NORMAL|cv::WINDOW_GUI_EXPANDED);
    cv::resizeWindow("Control Panel", m_panel_w, std::min(m_panel_h, 700));
    cv::setMouseCallback("Control Panel", onMouse, this);
}

// ── Mouse callback ───────────────────────────────────────────────

void UIController::onMouse(int event, int x, int y, int flags, void* userdata) {
    ((UIController*)userdata)->handleMouse(event, x, y, flags);
}

void UIController::handleMouse(int event, int x, int y, int flags) {
    m_mouse_x = x; m_mouse_y = y;

    // ── build slider geometry for hit-test + drag ──
    // We'll compute slider Y positions the same way drawControlPanel does
    struct SliderGeo { int y_top; int tx; int tw; };
    std::vector<SliderGeo> geo(m_sliders.size());

    int cy = 40 - m_scroll_y;
    for (auto& sec : m_sections) {
        if (sec.collapsed) { cy += SEC_H; continue; }
        cy += SEC_H;
        for (int sid : sec.slider_ids) {
            geo[sid].y_top = cy;
            // track X bounds (must match drawControlPanel layout)
            auto& sl = m_sliders[sid];
            char vbuf[32];
            if (sl.hint.empty()) snprintf(vbuf, sizeof(vbuf), "%d", *sl.val);
            else snprintf(vbuf, sizeof(vbuf), "%d %s", *sl.val, sl.hint.c_str());
            int vw = cv::getTextSize(vbuf, cv::FONT_HERSHEY_SIMPLEX, 0.38, 1, nullptr).width;
            geo[sid].tx = 100;
            geo[sid].tw = m_panel_w - 140 - vw - 10;
            cy += ROW_H;
        }
        cy += PAD;
    }

    // ── update hover ──
    m_hover_slider = -1;
    for (int i = 0; i < (int)m_sliders.size(); i++) {
        if (y >= geo[i].y_top && y < geo[i].y_top + ROW_H && x >= 8 && x < m_panel_w - 8) {
            m_hover_slider = i;
            break;
        }
    }

    // ── drag active slider ──
    if (event == cv::EVENT_MOUSEMOVE && m_active_slider >= 0) {
        auto& sl = m_sliders[m_active_slider];
        auto& g  = geo[m_active_slider];
        float frac = (float)(x - g.tx) / std::max(1, g.tw);
        frac = std::max(0.0f, std::min(1.0f, frac));
        *sl.val = sl.lo + (int)(frac * (sl.hi - sl.lo));
    }

    // ── mouse down: start drag or toggle section ──
    if (event == cv::EVENT_LBUTTONDOWN) {
        // check section header clicks
        int chy = 40 - m_scroll_y;
        for (auto& sec : m_sections) {
            if (y >= chy && y < chy + SEC_H && x >= 4 && x < m_panel_w - 4) {
                sec.collapsed = !sec.collapsed;
                updatePanelLayout();
                return;
            }
            chy += SEC_H;
            if (!sec.collapsed) chy += (int)sec.slider_ids.size() * ROW_H + PAD;
        }
        // start dragging whichever slider is hovered
        if (m_hover_slider >= 0) {
            m_active_slider = m_hover_slider;
            // immediately jump to click position
            auto& sl = m_sliders[m_active_slider];
            auto& g  = geo[m_active_slider];
            float frac = (float)(x - g.tx) / std::max(1, g.tw);
            frac = std::max(0.0f, std::min(1.0f, frac));
            *sl.val = sl.lo + (int)(frac * (sl.hi - sl.lo));
            m_dirty = true;
        }
    }

    if (event == cv::EVENT_LBUTTONUP) {
        m_active_slider = -1;
    }

    // mark dirty during drag
    if (event == cv::EVENT_MOUSEMOVE && m_active_slider >= 0) {
        m_dirty = true;
    }

    // mouse wheel scroll
    if (event == cv::EVENT_MOUSEWHEEL) {
        int total = 40;
        for (auto& sec : m_sections) { total += SEC_H; if (!sec.collapsed) total += (int)sec.slider_ids.size() * ROW_H + PAD; }
        int mx = std::max(0, total - m_panel_h);
        m_scroll_y = std::max(0, std::min(mx, m_scroll_y - cv::getMouseWheelDelta(flags) * 20));
    }
}

// ── Render ───────────────────────────────────────────────────────

void UIController::drawControlPanel() {
    int W = m_panel_w;
    cv::Mat panel(m_panel_h, W, CV_8UC3, cv::Scalar(14,16,20)); // dark bg

    // ── header ──
    cv::rectangle(panel, cv::Rect(0,0,W,36), cv::Scalar(10,12,16), -1);
    cv::line(panel, cv::Point(0,35), cv::Point(W,35), cv::Scalar(0,180,180), 1, cv::LINE_AA);
    glowText(panel, "CONTROL PANEL", cv::Point(12,26), 0.55, cv::Scalar(0,220,255), 2);

    // ── sections ──
    int cy = 40 - m_scroll_y;

    for (auto& sec : m_sections) {
        // skip if scrolled off screen
        if (cy + SEC_H < 0) { cy += SEC_H; if(!sec.collapsed)cy+=(int)sec.slider_ids.size()*ROW_H+PAD; continue; }
        if (cy > m_panel_h) break;

        // section header
        cv::Scalar hdr_bg = sec.collapsed ? cv::Scalar(22,25,32) : cv::Scalar(28,32,40);
        cv::rectangle(panel, cv::Rect(4, cy, W-8, SEC_H), hdr_bg, -1);
        cv::rectangle(panel, cv::Rect(4, cy, W-8, SEC_H), cv::Scalar(50,55,65), 1);
        cv::Scalar hdr_accent = sec.name=="CAMERA"?cv::Scalar(255,180,0):
                                sec.name.find("RED")!=std::string::npos?cv::Scalar(0,69,255):
                                sec.name.find("BLUE")!=std::string::npos?cv::Scalar(255,140,0):
                                sec.name=="FILTER"?cv::Scalar(0,220,200):cv::Scalar(160,160,180);
        cv::rectangle(panel, cv::Rect(4, cy, 3, SEC_H), hdr_accent, -1);
        std::string hdr_txt = (sec.collapsed ? "+ " : "- ") + sec.name;
        cv::putText(panel, hdr_txt, cv::Point(16, cy+21), cv::FONT_HERSHEY_SIMPLEX, 0.50, cv::Scalar(200,205,215), 1, cv::LINE_AA);
        cy += SEC_H;

        if (!sec.collapsed) {
            for (int sid : sec.slider_ids) {
                if (cy + ROW_H < 0) { cy += ROW_H; continue; }
                if (cy > m_panel_h) break;
                auto& sl = m_sliders[sid];
                int val = *sl.val;
                float frac = (float)(val - sl.lo) / std::max(1, sl.hi - sl.lo);
                frac = std::max(0.0f, std::min(1.0f, frac));

                bool active = (sid == m_active_slider);
                bool hover  = (sid == m_hover_slider);

                // row bg
                cv::Scalar row_bg = active ? cv::Scalar(35,40,55) : hover ? cv::Scalar(25,28,38) : cv::Scalar(18,20,26);
                cv::rectangle(panel, cv::Rect(8, cy, W-16, ROW_H), row_bg, -1);

                // label
                cv::putText(panel, sl.name, cv::Point(14, cy+12), cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(180,185,195), 1, cv::LINE_AA);

                // value text
                char vbuf[32];
                if (sl.hint.empty()) snprintf(vbuf, sizeof(vbuf), "%d", val);
                else snprintf(vbuf, sizeof(vbuf), "%d %s", val, sl.hint.c_str());
                int vw = cv::getTextSize(vbuf, cv::FONT_HERSHEY_SIMPLEX, 0.38, 1, nullptr).width;
                cv::putText(panel, vbuf, cv::Point(W - 20 - vw, cy+12), cv::FONT_HERSHEY_SIMPLEX, 0.38, cv::Scalar(140,200,220), 1, cv::LINE_AA);

                // track
                int tx = 100, tw = W - 140 - vw - 10, track_y = cy + 18, track_h = 5;
                cv::Rect track_rect(tx, track_y, tw, track_h);
                cv::rectangle(panel, track_rect, cv::Scalar(40,44,52), -1);
                cv::rectangle(panel, track_rect, cv::Scalar(60,65,75), 1);

                // fill
                int fill_w = (int)(tw * frac);
                if (fill_w > 0) {
                    cv::Scalar fill_c = active?cv::Scalar(0,200,255):hover?cv::Scalar(0,180,230):hdr_accent;
                    cv::rectangle(panel, cv::Rect(tx, track_y, fill_w, track_h), fill_c, -1);
                }

                // knob
                int kx = tx + (int)(tw * frac);
                cv::Scalar knob_c = active?cv::Scalar(0,240,255):hover?cv::Scalar(200,220,255):cv::Scalar(180,190,200);
                cv::circle(panel, cv::Point(kx, track_y + track_h/2), active?7:hover?5:4, knob_c, -1, cv::LINE_AA);
                cv::circle(panel, cv::Point(kx, track_y + track_h/2), active?9:hover?7:6, cv::Scalar(0,0,0), 1, cv::LINE_AA);

                cy += ROW_H;
            }
            cy += PAD;
        }
    }

    // ── scrollbar indicator (right edge) ──
    int total_content = 40; // header
    for (auto& sec : m_sections) { total_content += SEC_H; if (!sec.collapsed) total_content += (int)sec.slider_ids.size() * ROW_H + PAD; }
    if (total_content > m_panel_h) {
        int sb_x = W - 6, sb_h = m_panel_h - 50;
        float view_ratio = (float)m_panel_h / total_content;
        int thumb_h = std::max(20, (int)(sb_h * view_ratio));
        float scroll_frac = (float)m_scroll_y / std::max(1, total_content - m_panel_h);
        int thumb_y = 38 + (int)((sb_h - thumb_h) * scroll_frac);
        cv::rectangle(panel, cv::Rect(sb_x, 38, 4, sb_h), cv::Scalar(35,38,48), -1);
        cv::rectangle(panel, cv::Rect(sb_x, thumb_y, 4, thumb_h), cv::Scalar(80,85,100), -1);
    }

    // ── footer hint ──
    int fy = m_panel_h - 22;
    cv::rectangle(panel, cv::Rect(0, fy-4, W, 28), cv::Scalar(10,12,16), -1);
    cv::putText(panel, "DRAG sliders  |  CLICK header to expand", cv::Point(12, fy+10),
                cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(120,125,135), 1, cv::LINE_AA);

    cv::imshow("Control Panel", panel);
}

// ── sync slider values back to processor/camera ──────────────────

void UIController::syncFromSliders(ImageProcessor* processor, CameraManager* camera) {
    if (!m_dirty) return;
    m_dirty = false;

    camera->setExposure(camera->exposure);
    camera->setGain(camera->gain);
    processor->red1.h_lo = 0;
    processor->red1.v_lo = processor->red1.s_lo;
    processor->red1.s_hi = 255; processor->red1.v_hi = 255;
    processor->red2.h_lo  = 180 - processor->red1.h_hi;
    processor->red2.h_hi = 180;
    processor->red2.s_lo = processor->red1.s_lo;
    processor->red2.v_lo      = processor->red1.s_lo;
    processor->red2.s_hi = 255; processor->red2.v_hi = 255;
    processor->blue_range.h_hi = std::min(processor->blue_range.h_lo + processor->blue_range.h_hi, 180);
    processor->blue_range.s_hi = 255; processor->blue_range.v_hi = 255;
    processor->blue_range.v_lo = processor->blue_range.s_lo;
    processor->updateColorRanges();
}

// ═══════════════════════════════════════════════════════════════════
//  BINARY DISPLAY
// ═══════════════════════════════════════════════════════════════════

void UIController::drawBinaryImages(ImageProcessor* processor) {
    if(!show_binary)return;
    cv::Mat hsv=processor->getHSV();if(hsv.empty())return;
    cv::Mat r1,r2,rm,bm;
    cv::inRange(hsv,cv::Scalar(processor->red1.h_lo,processor->red1.s_lo,processor->red1.v_lo),cv::Scalar(processor->red1.h_hi,processor->red1.s_hi,processor->red1.v_hi),r1);
    cv::inRange(hsv,cv::Scalar(processor->red2.h_lo,processor->red2.s_lo,processor->red2.v_lo),cv::Scalar(processor->red2.h_hi,processor->red2.s_hi,processor->red2.v_hi),r2);
    cv::bitwise_or(r1,r2,rm);
    cv::inRange(hsv,cv::Scalar(processor->blue_range.h_lo,processor->blue_range.s_lo,processor->blue_range.v_lo),cv::Scalar(processor->blue_range.h_hi,processor->blue_range.s_hi,processor->blue_range.v_hi),bm);
    if(!rm.empty())cv::imshow("Red Binary",rm);
    if(!bm.empty())cv::imshow("Blue Binary",bm);
}

// ── Toast notifications ──────────────────────────────────────────

void UIController::showToast(const std::string& msg) {
    m_toasts.push_back({msg, std::chrono::steady_clock::now() + std::chrono::milliseconds(1500)});
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
}

void UIController::drawToasts(cv::Mat& image) {
    auto now = std::chrono::steady_clock::now();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
        [&](const Toast& t){ return now > t.expiry; }), m_toasts.end());
    if (m_toasts.empty()) return;
    int cx = image.cols/2, base_y = image.rows/2 - 30;
    for (size_t i = 0; i < m_toasts.size(); i++) {
        float alpha = std::max(0.0f, std::min(1.0f,
            std::chrono::duration<float>(m_toasts[i].expiry - now).count() / 0.5f));
        cv::Size ts = cv::getTextSize(m_toasts[i].text, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, nullptr);
        int tx = cx - ts.width/2, ty = base_y + (int)i * 30;
        cv::putText(image, m_toasts[i].text, cv::Point(tx, ty),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,0,0), 3, cv::LINE_AA);
        cv::Scalar c(255*alpha, 255*alpha, 255*alpha);
        cv::putText(image, m_toasts[i].text, cv::Point(tx, ty),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, c, 2, cv::LINE_AA);
    }
}

// ── Toggles ──────────────────────────────────────────────────────

void UIController::toggleUI(){show_ui=!show_ui;std::cout<<"UI "<<(show_ui?"ON":"OFF")<<std::endl;}
void UIController::toggleBinary(){show_binary=!show_binary;std::cout<<"Binary "<<(show_binary?"ON":"OFF")<<std::endl;if(!show_binary){cv::destroyWindow("Red Binary");cv::destroyWindow("Blue Binary");}}
void UIController::startNewRound(){round_status=1;is_recognition_allowed=false;std::cout<<"New round. Press A to arm."<<std::endl;}
void UIController::allowRecognition(){if(round_status>0&&round_status<3){is_recognition_allowed=true;std::cout<<"ARMED (round "<<round_status<<")"<<std::endl;}else std::cout<<"Start round first (R)."<<std::endl;}
void UIController::finishRecognition(){if(round_status>0&&round_status<3){round_status++;is_recognition_allowed=false;std::cout<<"Finished. Round: "<<round_status<<std::endl;}else std::cout<<"No active round."<<std::endl;}
void UIController::updateFPS(double fps){current_fps=fps;}
