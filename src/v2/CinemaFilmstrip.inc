// Visible video frames only: a second decoder keeps seeks away from the preview.
// Textures are bounded and released with their document; decoding never runs on the UI/audio thread.
void FoundryView::releaseCinemaThumbnails(Tab& t)
{
    t.thumbnailDecoder.reset();
    for (const auto& [frame, thumbnail] : t.thumbnails) host_.releaseTexture(thumbnail.key);
    t.thumbnails.clear();t.thumbnailSource.clear();t.thumbnailPending=-1;
}
void FoundryView::drawCinemaFilmstrip(Tab& t, ImVec2 top, ImVec2 bottom)
{
    cinemaStripMin_=top;cinemaStripMax_=bottom;
    const auto snapshot=t.document->shared();const auto& s=*snapshot;const auto& picture=s.picture;
    auto* draw=ImGui::GetWindowDrawList();const auto& p=theme::palette();
    const double fps=std::clamp(picture.fps,1.0,240.0);
    const auto source=fromUtf8(picture.file)+"|"+juce::String(fps,6);
    if(source!=t.thumbnailSource){releaseCinemaThumbnails(t);t.thumbnailSource=source;t.thumbnailDecoder=std::make_unique<CinemaVideo>(160);}
    if(t.thumbnailPending>=0){
        const auto frame=t.thumbnailDecoder->snapshot();
        if(frame.image.isValid()&&std::abs(frame.time-t.thumbnailPending/fps)<.6/std::min(fps,frame.fps)){
            const auto key=t.videoTextureKey+"-thumb-"+juce::String(t.thumbnailPending);
            t.thumbnails[t.thumbnailPending]={host_.updateTexture(key,frame.image),key,++t.thumbnailClock,static_cast<float>(frame.image.getWidth())/frame.image.getHeight()};
            t.thumbnailPending=-1;
        }else if(frame.error.isNotEmpty())t.thumbnailPending=-2;
    }
    // At close zoom the stride becomes one frame. At overview zoom it is sampled,
    // aligned to source frame numbers so panning reuses existing thumbnails.
    const double pixelsPerSecond=(bottom.x-top.x)/t.cinemaVisibleSeconds;
    const auto stride=std::max<std::int64_t>(1,static_cast<std::int64_t>(std::ceil(fps*dp(104)/pixelsPerSecond)));
    const double viewStart=t.viewStart/s.rate;
    const auto first=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::floor((viewStart-picture.start+picture.sourceIn)*fps/stride))*stride);
    const auto xOf=[&](double seconds){return top.x+static_cast<float>((seconds-viewStart)*pixelsPerSecond);};
    const auto duration=t.video?t.video->snapshot().duration:0.0;
    const auto pictureEnd=picture.start+(duration>0?std::min(picture.duration,std::max(0.0,duration-picture.sourceIn)):picture.duration);
    std::vector<std::int64_t> wanted;
    draw->AddRectFilled(top,bottom,p.bg0);draw->PushClipRect(top,bottom,true);
    for(auto frame=first;wanted.size()<96;frame+=stride){
        const double at=picture.start+frame/fps-picture.sourceIn;
        if(at>=viewStart+t.cinemaVisibleSeconds||at>=pictureEnd)break;
        if(at+stride/fps<=picture.start||at+stride/fps<=viewStart)continue;
        const auto left=std::max(top.x,xOf(std::max(at,picture.start)));
        const auto right=std::min(bottom.x,xOf(std::min(at+stride/fps,pictureEnd)));
        if(right<=left)continue;
        wanted.push_back(frame);
        const auto found=t.thumbnails.find(frame);
        const ImVec2 imageTop(left,top.y),imageBottom(right,bottom.y-dp(16));
        if(found!=t.thumbnails.end()){
            found->second.used=++t.thumbnailClock;
            const auto fullLeft=xOf(at),fullWidth=static_cast<float>(stride/fps*pixelsPerSecond);
            const auto imageWidth=std::min(fullWidth,(imageBottom.y-imageTop.y)*found->second.aspect);
            const auto imageHeight=imageWidth/found->second.aspect;
            const ImVec2 fitted(fullLeft+(fullWidth-imageWidth)*.5f,imageTop.y+(imageBottom.y-imageTop.y-imageHeight)*.5f);
            draw->AddRectFilled(imageTop,imageBottom,IM_COL32(0,0,0,255));draw->PushClipRect(imageTop,imageBottom,true);
            draw->AddImage(static_cast<ImTextureID>(found->second.texture),fitted,ImVec2(fitted.x+imageWidth,fitted.y+imageHeight));draw->PopClipRect();
        }else draw->AddRectFilled(imageTop,imageBottom,p.bg2);
        draw->AddLine(ImVec2(left,top.y),ImVec2(left,bottom.y),p.line);
        draw->AddText(ImVec2(left+dp(3),bottom.y-dp(14)),p.textDim,cinemaTime(std::max(picture.start,at),fps).toRawUTF8());
    }
    const auto head=xOf(transportState().seconds);
    if(head>=top.x&&head<bottom.x)draw->AddLine(ImVec2(head,top.y),ImVec2(head,bottom.y),p.accent,dp(2));
    draw->PopClipRect();
    // Cancel obsolete requests after a pan/zoom, otherwise finish the current frame.
    if(t.thumbnailPending>=0&&std::find(wanted.begin(),wanted.end(),t.thumbnailPending)==wanted.end())t.thumbnailPending=-1;
    if(t.thumbnailPending==-1)for(const auto frame:wanted)if(!t.thumbnails.contains(frame)){
        t.thumbnailPending=frame;t.thumbnailDecoder->request(juce::File(fromUtf8(picture.file)),frame/fps);break;
    }
    while(t.thumbnails.size()>128){
        const auto oldest=std::min_element(t.thumbnails.begin(),t.thumbnails.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        host_.releaseTexture(oldest->second.key);t.thumbnails.erase(oldest);
    }
    ImGui::SetCursorScreenPos(top);ImGui::InvisibleButton("##cinemaFilmstrip",ImVec2(bottom.x-top.x,bottom.y-top.y));
    if(ImGui::IsItemActive()){
        TransportSync::Update update;update.state=transportState();
        update.state.seconds=std::max(0.0,std::round((viewStart+(ImGui::GetIO().MousePos.x-top.x)/pixelsPerSecond)*fps)/fps);
        update.position=true;applyTransport(update);
    }
    ui::tooltip(L("Vidéo : glisser pour parcourir les images ; Ctrl+molette pour zoomer jusqu'à chaque image", "Video: drag to scrub; Ctrl+wheel to zoom to individual frames"));
    if(t.thumbnailPending>=0)host_.wake(2);
}
