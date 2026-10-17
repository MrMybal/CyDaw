// Arrangement-style Cinema timeline. Bus rows share one ruler and scale.
void FoundryView::fitCinemaContainers(Session& session) const {
    for(auto& track:session.tracks)if(!track.bus){const auto [start,end]=cinemaBounds(track,session.rate);track.containerStart=start;track.containerLength=end-start;}
}
void FoundryView::focusCinemaTrack(Tab& t,std::uint64_t id){
    const auto& s=t.document->session();const auto index=s.trackIndex(id);if(index<0||s.tracks[index].bus)return;
    const auto& track=s.tracks[index];const auto [start,end]=cinemaBounds(track,s.rate);
    t.activeGroup=track.parentId;t.collapsedGroups.erase(track.parentId);t.cinemaContentsVisible=true;
    t.cinemaVisibleSeconds=std::clamp((end-start)/s.rate*1.3,1.0,7200.0);
    t.viewStart=std::max(0.0,start-t.cinemaVisibleSeconds*s.rate*.12);t.cinemaFocusTrack=id;
    pickTrack(t,index,false);state_.setBottomPanelVisible(true);host_.wake(8);
}
bool FoundryView::changeCinemaContainer(Tab& t,std::uint64_t id,std::int64_t start,std::int64_t length,bool move){
    auto next=t.document->session();const auto index=next.trackIndex(id);if(index<0||next.tracks[index].bus)return false;
    cinemaPlace(next.tracks[index],start,length,move,next.rate);
    edit(t,std::move(next),ui::Ls(move?"Déplacer la sous-piste":"Redimensionner le conteneur",move?"Move subtrack":"Resize container"));return true;
}
void FoundryView::drawCinemaClipLane(Tab& t,int index,ImVec2 top,ImVec2 bottom,float timelineLeft,float timelineRight){
    const auto snapshot=t.document->shared();if(index<0||index>=static_cast<int>(snapshot->tracks.size()))return;
    const auto& session=*snapshot;const auto& track=session.tracks[index];const auto& palette=theme::palette();
    auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(top,bottom,palette.bg0);
    lanesMin_=ImVec2(timelineLeft,top.y);lanesMax_=bottom;
    const auto snap=[&](double frame){return std::max<std::int64_t>(0,static_cast<std::int64_t>(t.cinemaSnap&&!ImGui::GetIO().KeyAlt?std::round(frame/session.rate*session.picture.fps/t.cinemaGridFrames)*t.cinemaGridFrames/session.picture.fps*session.rate:frame));};
    const auto frameAt=[&](float x){return snap(t.viewStart+(x-timelineLeft)*t.framesPerPixel);};
    const auto xOf=[&](double frame){return timelineLeft+static_cast<float>((frame-t.viewStart)/t.framesPerPixel);};
    const float laneWidth=bottom.x-top.x;
    const auto mouse=ImGui::GetIO().MousePos;
    const bool overClip=mouse.y>=top.y&&mouse.y<bottom.y&&std::any_of(track.clips.begin(),track.clips.end(),[&](const Clip& c){return mouse.x>=xOf(c.start)&&mouse.x<xOf(c.end());});
    struct Transfer {std::uint64_t clip;std::int64_t grab;};
    draw->PushClipRect(top,bottom,true);
    if(track.clips.empty())draw->AddText(ImVec2(top.x+dp(8),top.y+dp(36)),palette.textFaint,
        track.midi?L("Double-clic : clip MIDI","Double click: MIDI clip"):L("Glissez un son ici","Drop a sound here"));
    for(const auto& clip:track.clips){
        const auto left=std::max(top.x,xOf(clip.start)),right=std::min(bottom.x,xOf(clip.end()));if(right<=left)continue;
        drawClip(t,clip,index,top,bottom,timelineLeft,timelineRight,track.midi?IM_COL32(171,128,220,255):IM_COL32(70,172,210,255));
        ImGui::PushID(static_cast<int>(clip.id));ImGui::SetCursorScreenPos(ImVec2(left,top.y));
        ImGui::InvisibleButton("##cinemaClip",ImVec2(std::max(dp(3),right-left),bottom.y-top.y),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
        const auto& io=ImGui::GetIO();
        if(ImGui::IsItemClicked(0)||ImGui::IsItemClicked(1)){t.clip=clip.id;t.cursor=frameAt(io.MousePos.x);pickTrack(t,index,false);t.processing->selectInput(track.id);t.detailsFocus=true;}
        const bool nearLeft=std::abs(io.MousePos.x-xOf(clip.start))<dp(6),nearRight=std::abs(io.MousePos.x-xOf(clip.end()))<dp(6);
        if(ImGui::IsItemHovered()&&(nearLeft||nearRight))ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if(ImGui::IsItemActivated()&&io.MouseDown[0]&&(nearLeft||nearRight)){cinemaGestureBefore_=t.document->session();cinemaGestureClip_=clip;cinemaGestureTrack_=index;cinemaGestureX_=io.MousePos.x;cinemaGestureKind_=nearLeft?1:2;}
        if(ImGui::IsItemActive()&&cinemaGestureBefore_&&ImGui::IsMouseDragging(0)){
            const auto delta=static_cast<std::int64_t>((io.MousePos.x-cinemaGestureX_)*t.framesPerPixel);
            auto next=*cinemaGestureBefore_;auto& edited=next.tracks[cinemaGestureTrack_].clips;
            for(auto& c:edited)if(c.id==clip.id){const auto old=cinemaGestureClip_;if(cinemaGestureKind_==1){
                const auto trim=std::clamp<std::int64_t>(snap(old.start+delta)-old.start,-std::min(old.offset,old.start),old.length-1);
                c.start=old.start+trim;c.offset=old.offset+trim;c.length=old.length-trim;
            }else c.length=std::clamp<std::int64_t>(snap(old.end()+delta)-old.start,1,old.sound?old.sound->frames()-old.offset:static_cast<std::int64_t>(session.rate*3600));
            c.fadeIn=std::min(c.fadeIn,c.length);c.fadeOut=std::min(c.fadeOut,c.length-c.fadeIn);}
            t.document->replace(std::move(next));if(playingTab_==current_)this->player().setSession(t.document->shared());
        }
        if(!cinemaGestureBefore_&&ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){
            const Transfer transfer{clip.id,std::clamp<std::int64_t>(frameAt(io.MouseClickedPos[0].x)-clip.start,0,clip.length)};
            ImGui::SetDragDropPayload("CYDAW_CINEMA_CLIP",&transfer,sizeof(transfer));ImGui::TextUnformatted(clip.name.c_str());ImGui::EndDragDropSource();
        }
        if(ImGui::IsItemClicked(1))ImGui::OpenPopup("##cinemaClipMenu");
        if(ImGui::BeginPopup("##cinemaClipMenu")){clipMenu(t);ImGui::EndPopup();}
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)&&track.midi)showMidiEditor(t);
        ImGui::PopID();
    }
    // Submit the empty-lane target only outside clips. A late overlapping item
    // otherwise steals the next frame's hovered/active ID from the real clip.
    if(!overClip||ImGui::GetDragDropPayload()!=nullptr){
    ImGui::SetCursorScreenPos(top);ImGui::InvisibleButton("##cinemaLane",ImVec2(laneWidth,bottom.y-top.y),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
    if(!overClip&&ImGui::IsItemHovered()&&ImGui::IsMouseClicked(0)){t.cursor=frameAt(ImGui::GetIO().MousePos.x);pickTrack(t,index,false);t.processing->selectInput(track.id);}
    if(ImGui::IsItemClicked(1)){t.cursor=frameAt(mouse.x);t.clip=0;pickTrack(t,index,false);t.processing->selectInput(track.id);ImGui::OpenPopup("##cinemaLaneMenu");}
    if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){
        t.cursor=frameAt(mouse.x);
        if(track.midi)createMidiClip(t,index,t.cursor);else chooseCinemaClipAudio(t,track.id,t.cursor);
    }
    if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload(kFilePayload);payload&&payload->DataSize>1){
            const auto file=juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data),payload->DataSize-1));
            if(cinemaVideoFile(file))openCinemaVideo(file);else importFile(t,file,index,frameAt(ImGui::GetIO().MousePos.x));
        }
        if(const auto* payload=ImGui::AcceptDragDropPayload("CYDAW_CINEMA_CLIP",ImGuiDragDropFlags_AcceptBeforeDelivery);payload&&payload->DataSize==sizeof(Transfer)){
            const auto data=*static_cast<const Transfer*>(payload->Data);auto next=t.document->session();int from{},clipIndex{};
            if(next.findClip(data.clip,from,clipIndex)&&next.tracks[from].midi==track.midi){
                auto clip=next.tracks[from].clips[clipIndex];clip.start=std::max<std::int64_t>(0,frameAt(ImGui::GetIO().MousePos.x)-data.grab);
                const auto left=xOf(clip.start),right=xOf(clip.end());draw->AddRect(ImVec2(left,top.y),ImVec2(right,bottom.y),palette.accent,dp(2),dp(2));
                if(payload->IsDelivery()){
        if(ImGui::GetIO().KeyCtrl){clip.id=next.newId();for(auto& note:clip.notes)note.id=next.newId();}
        else next.tracks[from].clips.erase(next.tracks[from].clips.begin()+clipIndex);
        const auto destination=next.trackIndex(track.id);next.tracks[destination].clips.push_back(std::move(clip));t.clip=next.tracks[destination].clips.back().id;
        edit(t,std::move(next),ui::Ls("Déplacer / copier le clip","Move / copy clip"));pickTrack(t,destination,false);t.processing->selectInput(track.id);
                }
            }
        }ImGui::EndDragDropTarget();
    }
    } // no clip under the pointer
    if(ImGui::BeginPopup("##cinemaLaneMenu")){
        if(track.midi){
            if(ImGui::MenuItem(L("Créer un clip MIDI ici", "Create MIDI clip here")))createMidiClip(t,index,t.cursor);
            if(ImGui::MenuItem(L("Choisir l'instrument…", "Choose instrument…")))t.processing->chooseInstrument(track.id);
        }else if(ImGui::MenuItem(L("Créer un clip audio vide ici", "Create empty audio clip here")))createCinemaAudioClip(t,index,t.cursor);
        if(ImGui::MenuItem(L("Importer un son ici…", "Import sound here…")))chooseCinemaClipAudio(t,track.id,t.cursor);
        ImGui::Separator();
        if(ImGui::MenuItem(L("Coller (Ctrl+V)", "Paste (Ctrl+V)"),nullptr,false,clipboard_.has_value()))paste(t,false);
        if(ImGui::MenuItem(L("Effets de la sous-piste", "Subtrack effects"))){t.processing->selectInput(track.id);t.processing->showPanel(0);state_.setBottomPanelVisible(true);}
        ImGui::EndPopup();
    }
    const auto head=transportState().seconds*session.rate;const auto x=xOf(head);
    if(x>=top.x&&x<bottom.x)draw->AddLine(ImVec2(x,top.y),ImVec2(x,bottom.y),palette.accent,dp(2));
    draw->PopClipRect();
}
void FoundryView::drawCinemaSubtracks(Tab& t){
    const auto snapshot=t.document->shared();const auto& s=*snapshot;const auto bus=s.trackIndex(t.activeGroup);
    if(bus<0||!s.tracks[bus].bus){ImGui::TextDisabled("%s",L("Sélectionnez un bus.","Select a bus."));return;}
    ImGui::Text("%s  —  %s",s.tracks[bus].name.c_str(),L("Sous-pistes dans la timeline","Subtracks in the timeline"));
    if(ImGui::Button(L("+ Sous-piste audio","+ Audio subtrack")))addCinemaTrack(t,t.activeGroup,false);
    ImGui::SameLine();if(ImGui::Button(L("+ Sous-piste MIDI","+ MIDI subtrack")))addCinemaTrack(t,t.activeGroup,true);
    if(ImGui::BeginTable("##cinemaSubtrackList",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY)){
        ImGui::TableSetupColumn(L("Sous-piste","Subtrack"),ImGuiTableColumnFlags_WidthStretch);
        for(const auto* label:{L("Type","Type"),L("Début","Start"),L("Durée","Length"),"Clips",L("Actions","Actions")})ImGui::TableSetupColumn(label,ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();int count=0;
        for(int i=0;i<static_cast<int>(s.tracks.size());++i){const auto& tr=s.tracks[i];if(tr.bus||tr.parentId!=t.activeGroup)continue;
            ++count;ImGui::PushID(static_cast<int>(tr.id));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            const bool selected=i<static_cast<int>(t.tracks.size())&&t.tracks[i];
            if(ImGui::Selectable(tr.name.c_str(),selected,ImGuiSelectableFlags_AllowDoubleClick)){pickTrack(t,i,false);if(ImGui::IsMouseDoubleClicked(0))focusCinemaTrack(t,tr.id);}
            ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(tr.midi?"MIDI":"Audio");const auto [a,b]=cinemaBounds(tr,s.rate);
            ImGui::TableSetColumnIndex(2);ImGui::Text("%.3f s",a/s.rate);ImGui::TableSetColumnIndex(3);ImGui::Text("%.3f s",(b-a)/s.rate);
            ImGui::TableSetColumnIndex(4);ImGui::Text("%d",static_cast<int>(tr.clips.size()));ImGui::TableSetColumnIndex(5);
            if(ImGui::SmallButton(L("Cadrer","Focus")))focusCinemaTrack(t,tr.id);
            cinemaListTargets_.push_back({ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),tr.id});
            ImGui::SameLine();if(ImGui::SmallButton("FX")){pickTrack(t,i,false);t.processing->showPanel(0);}
            ImGui::SameLine();if(ImGui::SmallButton("A")){if(!t.automationRows.erase(tr.id))t.automationRows.insert(tr.id);focusCinemaTrack(t,tr.id);}
            ImGui::PopID();
        }
        if(!count){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextDisabled("%s",L("Double-cliquez sur la timeline du bus pour créer un conteneur.","Double click the bus timeline to create a container."));}
        ImGui::EndTable();
    }
}
void FoundryView::drawCinemaTracks(Tab& t,ImVec2 size){
    cinemaDropTargets_.clear();cinemaBusTargets_.clear();
    const auto snapshot=t.document->shared();const auto& s=*snapshot;const auto& p=theme::palette();const auto& io=ImGui::GetIO();
    if(cinemaContainerGesture_&&cinemaContainerGesture_->document!=t.cinemaIdentity)cinemaContainerGesture_.reset();
    const float headerWidth=dp(200),navigatorHeight=dp(14),rulerHeight=dp(22);
    const auto origin=ImGui::GetCursorScreenPos();const auto wholeWidth=ImGui::GetContentRegionAvail().x;
    const ImVec2 rulerTop(origin.x,origin.y+navigatorHeight);
    const float laneWidth=std::max(dp(60),wholeWidth-headerWidth-dp(16));
    const float timelineLeft=rulerTop.x+headerWidth,timelineRight=timelineLeft+laneWidth;
    if(!player().playing())cinemaFollowPaused_=false;
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)&&io.MouseWheel!=0){
        if((io.KeyCtrl||io.KeyShift)&&player().playing())cinemaFollowPaused_=true;
        if(io.KeyCtrl)zoomCinema(t,std::pow(1.15,io.MouseWheel),std::clamp((io.MousePos.x-timelineLeft)/laneWidth,0.f,1.f));
        else if(io.KeyShift)t.viewStart=std::max(0.0,t.viewStart-io.MouseWheel*t.cinemaVisibleSeconds*.1*s.rate);
    }
    if(cinemaFollow_&&!cinemaFollowPaused_&&player().playing()){
        const auto seconds=transportState().seconds;
        if(seconds<t.viewStart/s.rate||seconds>t.viewStart/s.rate+t.cinemaVisibleSeconds*.95)
            t.viewStart=std::max(0.0,seconds-t.cinemaVisibleSeconds*.15)*s.rate;
    }
    // Shared navigator used by the other timelines: a separate grab band above
    // the time labels. Keep the grabbed time beneath the pointer in both axes.
    cinemaNavigatorMin_=ImVec2(timelineLeft,origin.y);
    cinemaNavigatorMax_=ImVec2(timelineRight,rulerTop.y);
    auto position=t.viewStart/s.rate;
    auto pixelsPerSecond=laneWidth/t.cinemaVisibleSeconds;
    ImGui::PushID(static_cast<int>(current_));
    const bool fitView=ui::timelineBand("##cinemaNavigator",cinemaNavigatorMin_,cinemaNavigatorMax_,position,pixelsPerSecond,
        laneWidth/7200.0,laneWidth/.1,1/std::clamp(s.picture.fps,1.0,240.0),1.0);
    if(ImGui::IsItemActive()&&player().playing())cinemaFollowPaused_=true;
    ImGui::PopID();
    t.viewStart=position*s.rate;t.cinemaVisibleSeconds=laneWidth/pixelsPerSecond;
    if(fitView)fitCinema(t);
    t.framesPerPixel=t.cinemaVisibleSeconds*s.rate/laneWidth;
    const auto snap=[&](double frame){return std::max<std::int64_t>(0,static_cast<std::int64_t>(t.cinemaSnap&&!io.KeyAlt?std::round(frame/s.rate*s.picture.fps/t.cinemaGridFrames)*t.cinemaGridFrames/s.picture.fps*s.rate:frame));};
    const auto frameAt=[&](float x){return snap(t.viewStart+(x-timelineLeft)*t.framesPerPixel);};
    const auto xOf=[&](double frame){return timelineLeft+static_cast<float>((frame-t.viewStart)/t.framesPerPixel);};
    const double fps=std::clamp(s.picture.fps,1.0,240.0);
    const auto desired=t.cinemaVisibleSeconds*dp(80)/laneWidth;
    double step=std::max(1.0,std::ceil(desired*fps))/fps;
    for(double value:{1/fps,2/fps,5/fps,10/fps,.5,1.,2.,5.,10.,30.,60.,120.,300.,600.,1800.}){
        const auto aligned=std::max(1.0,std::round(value*fps))/fps;
        if(aligned>=desired){step=aligned;break;}
    }
    auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(rulerTop,ImVec2(timelineRight,rulerTop.y+rulerHeight),p.bg1);
    draw->AddText(ImVec2(rulerTop.x+dp(8),rulerTop.y+dp(5)),p.textDim,L("BUS","BUS"));
    const auto grid=[&](ImDrawList* list,float top,float bottom,bool labels){
        list->PushClipRect(ImVec2(timelineLeft,top),ImVec2(timelineRight,bottom),true);
        const auto seconds=t.viewStart/s.rate;
        for(double at=std::floor(seconds/step)*step;at<seconds+t.cinemaVisibleSeconds;at+=step){const auto x=xOf(at*s.rate);
            list->AddLine(ImVec2(x,top),ImVec2(x,bottom),p.gridStrong);
            if(!labels)for(int sub=1;sub<4;++sub){const auto minor=xOf((at+std::round(step*fps*sub/4)/fps)*s.rate);list->AddLine(ImVec2(minor,top),ImVec2(minor,bottom),p.grid);}
            if(labels)list->AddText(ImVec2(x+dp(4),top+dp(5)),p.textDim,cinemaTime(at,s.picture.fps).toRawUTF8());
        }list->PopClipRect();
    };
    grid(draw,rulerTop.y,rulerTop.y+rulerHeight,true);
    ImGui::SetCursorScreenPos(ImVec2(timelineLeft,rulerTop.y));ImGui::InvisibleButton("##cinemaRuler",ImVec2(laneWidth,rulerHeight));
    if(ImGui::IsItemActive()){TransportSync::Update update;update.state=transportState();update.state.seconds=frameAt(io.MousePos.x)/s.rate;update.position=true;applyTransport(update);}
    float contentTop=rulerTop.y+rulerHeight;
    cinemaStripMin_=cinemaStripMax_=ImVec2(timelineLeft,contentTop);
    if(t.cinemaFilmstrip&&!s.picture.file.empty()){
        const auto stripHeight=dp(76);draw->AddRectFilled(ImVec2(rulerTop.x,contentTop),ImVec2(timelineLeft,contentTop+stripHeight),p.bg1);
        draw->AddText(ImVec2(rulerTop.x+dp(10),contentTop+dp(8)),p.textDim,L("VIDÉO", "VIDEO"));
        drawCinemaFilmstrip(t,ImVec2(timelineLeft,contentTop),ImVec2(timelineRight,contentTop+stripHeight));contentTop+=stripHeight;
    }
    ImGui::SetCursorScreenPos(ImVec2(rulerTop.x,contentTop));ImGui::Dummy(ImVec2(0,0));
    const auto rowsHeight=std::max(dp(50),ImGui::GetContentRegionAvail().y-dp(22));
    ImGui::BeginChild("##cinemaArrangementRows",ImVec2(0,rowsHeight),0,ImGuiWindowFlags_NoScrollWithMouse);
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)&&io.MouseWheel!=0&&!io.KeyCtrl&&!io.KeyShift)
        ImGui::SetScrollY(ImGui::GetScrollY()-io.MouseWheel*dp(54));
    const auto background=ImGui::GetCursorScreenPos();draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(timelineLeft,background.y),ImVec2(timelineRight,background.y+rowsHeight+ImGui::GetScrollY()),p.bg0);
    grid(draw,background.y,background.y+rowsHeight+ImGui::GetScrollY(),false);
    std::vector<std::uint64_t> buses;for(const auto& track:s.tracks)if(track.bus)buses.push_back(track.id);
    if(std::any_of(s.tracks.begin(),s.tracks.end(),[](const auto& tr){return !tr.bus&&!tr.parentId;}))buses.push_back(0);
    const auto queue=[&](const char* action,const char* op,std::uint64_t id){auto* object=new juce::DynamicObject();object->setProperty("action",action);object->setProperty("op",op);object->setProperty("id",static_cast<juce::int64>(id));cinemaPending_=juce::var(object);};
    const auto trackMix=[&](std::uint64_t id,bool mute,bool solo){auto next=t.document->session();const auto index=next.trackIndex(id);if(index<0)return;next.tracks[index].mute=mute;next.tracks[index].solo=solo;edit(t,std::move(next),ui::Ls("Réglages de piste","Track settings"));};
    for(const auto busId:buses){
        const auto busIndex=s.trackIndex(busId);const auto name=busIndex>=0?fromUtf8(s.tracks[busIndex].name):ui::Ls("Vers le master","To master");
        std::vector<int> children;for(int i=0;i<static_cast<int>(s.tracks.size());++i)if(!s.tracks[i].bus&&s.tracks[i].parentId==busId)children.push_back(i);
        std::stable_sort(children.begin(),children.end(),[&](int a,int b){return cinemaBounds(s.tracks[a],s.rate).first<cinemaBounds(s.tracks[b],s.rate).first;});
        std::vector<std::int64_t> laneEnds;std::map<int,int> slots;bool automation=false;
        for(int i:children){const auto [a,b]=cinemaBounds(s.tracks[i],s.rate);int lane=0;while(lane<static_cast<int>(laneEnds.size())&&laneEnds[lane]>a)++lane;
            if(lane==static_cast<int>(laneEnds.size()))laneEnds.push_back(b);else laneEnds[lane]=b;slots[i]=lane;automation=automation||t.automationRows.contains(s.tracks[i].id);}
        const bool collapsed=t.collapsedGroups.contains(busId);
        const float boxHeight=dp(t.cinemaContentsVisible?104:38)+(automation&&t.cinemaContentsVisible?dp(92):0);
        const float rowHeight=collapsed?dp(62):std::max(dp(72),static_cast<float>(laneEnds.size())*(boxHeight+dp(8))+dp(8));
        const auto origin=ImGui::GetCursorScreenPos();const ImVec2 laneTop(timelineLeft,origin.y),laneBottom(timelineRight,origin.y+rowHeight);
        draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,ImVec2(timelineLeft,origin.y+rowHeight),t.activeGroup==busId?p.bg2:p.bg1);
        draw->AddRectFilled(laneTop,laneBottom,p.bg0);draw->AddRectFilled(origin,ImVec2(origin.x+dp(3),origin.y+rowHeight),p.accent);grid(draw,laneTop.y,laneBottom.y,false);
        draw->AddLine(ImVec2(origin.x,laneBottom.y),laneBottom,p.line);draw->AddLine(laneTop,ImVec2(timelineLeft,laneBottom.y),p.line);
        cinemaBusTargets_.push_back({laneTop,laneBottom,busId});ImGui::PushID(static_cast<int>(busId));
        ImGui::SetCursorScreenPos(ImVec2(origin.x+dp(5),origin.y+dp(4)));
        if(ui::iconButton(collapsed?icon::chevronRight:icon::chevronDown,L("Replier / déplier le bus", "Collapse / expand bus"),false,0,ImVec2(dp(18),ImGui::GetFrameHeight()))){if(collapsed)t.collapsedGroups.erase(busId);else t.collapsedGroups.insert(busId);}
        ImGui::SameLine();if(ImGui::Selectable(name.toRawUTF8(),t.activeGroup==busId,0,ImVec2(headerWidth-dp(40),ImGui::GetFrameHeight()))){t.activeGroup=busId;t.masterSelected=false;t.processing->selectInput(busId);if(busId){state_.setBottomPanelVisible(true);t.processing->showPanel(5);}}
        if(ImGui::BeginPopupContextItem("##cinemaBusMenu")){
            if(busIndex>=0){char label[256]{};name.copyToUTF8(label,sizeof(label));if(ImGui::InputText(L("Nom du bus","Bus name"),label,sizeof(label))){auto next=t.document->session();next.tracks[busIndex].name=label;edit(t,std::move(next),ui::Ls("Renommer le bus","Rename bus"));}
                if(ImGui::MenuItem(L("Retirer le bus, garder les sous-pistes","Remove bus, keep subtracks")))queue("group","remove",busId);}
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x+dp(8),origin.y+dp(32)));
        if(busIndex>=0){auto mute=s.tracks[busIndex].mute,solo=s.tracks[busIndex].solo;
            if(ui::iconButton("M",L("Muet", "Mute"),mute,0,ImVec2(dp(22),dp(20))))trackMix(busId,!mute,solo);ImGui::SameLine(0,dp(2));if(ui::iconButton("S","Solo",solo,0,ImVec2(dp(22),dp(20))))trackMix(busId,mute,!solo);ImGui::SameLine(0,dp(2));}
        if(ImGui::SmallButton("FX")){t.activeGroup=busId;t.processing->selectInput(busId);t.processing->showPanel(0);state_.setBottomPanelVisible(true);}
        ImGui::SameLine();if(ImGui::SmallButton("A")){if(!t.automationRows.erase(busId))t.automationRows.insert(busId);}
        ImGui::SameLine();if(ImGui::SmallButton("+")){t.activeGroup=busId;ImGui::OpenPopup("##cinemaAddSubtrack");}
        if(ImGui::BeginPopup("##cinemaAddSubtrack")){if(ImGui::MenuItem(L("Sous-piste audio","Audio subtrack")))addCinemaTrack(t,busId,false);if(ImGui::MenuItem(L("Sous-piste MIDI","MIDI subtrack")))addCinemaTrack(t,busId,true);ImGui::EndPopup();}
        bool overContainer=false;
        if(!collapsed)for(int index:children){
            const auto& track=s.tracks[index];const auto [start,end]=cinemaBounds(track,s.rate);const auto rawLeft=xOf(start),rawRight=xOf(end);
            const float top=origin.y+dp(4)+slots[index]*(boxHeight+dp(8));
            if(t.cinemaFocusTrack==track.id){ImGui::SetScrollFromPosY(top-ImGui::GetWindowPos().y,0.35f);t.cinemaFocusTrack=0;}
            const float left=std::max(timelineLeft,rawLeft),right=std::min(timelineRight,rawRight);if(right<=left)continue;
            const ImVec2 boxMin(left,top),boxMax(right,top+boxHeight);if(!ImGui::IsRectVisible(boxMin,boxMax))continue;const auto accent=track.midi?IM_COL32(168,124,213,255):p.audio;
            const bool selected=index<static_cast<int>(t.tracks.size())&&t.tracks[index];
            draw->AddRectFilled(boxMin,boxMax,theme::alpha(accent,selected?.22f:.10f),dp(3));draw->AddRect(boxMin,boxMax,selected?p.text:accent,dp(2),dp(1),0);
            draw->AddRectFilled(boxMin,ImVec2(right,top+dp(23)),theme::alpha(accent,.45f),dp(3));
            draw->PushClipRect(boxMin,boxMax,true);draw->AddText(ImVec2(left+dp(7),top+dp(4)),p.text,track.name.c_str());draw->PopClipRect();
            ImGui::PushID(static_cast<int>(track.id));ImGui::SetCursorScreenPos(boxMin);
            ImGui::InvisibleButton("##cinemaContainerHeader",ImVec2(right-left,dp(23)),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
            if(ImGui::IsItemClicked(0)||ImGui::IsItemClicked(1)){t.activeGroup=busId;pickTrack(t,index,false);t.clip=0;}
            const bool edgeLeft=std::abs(io.MousePos.x-rawLeft)<dp(6),edgeRight=std::abs(io.MousePos.x-rawRight)<dp(6);
            if(ImGui::IsItemHovered()&&(edgeLeft||edgeRight))ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if(ImGui::IsItemActivated()&&io.MouseDown[0])cinemaContainerGesture_=CinemaContainerGesture{t.document->session(),track,t.cinemaIdentity,io.MousePos.x,edgeLeft?1:edgeRight?2:0,busId};
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){cinemaContainerGesture_.reset();focusCinemaTrack(t,track.id);t.processing->showPanel(5);}
            if(ImGui::BeginPopupContextItem("##cinemaContainerMenu")){
                char label[256]{};fromUtf8(track.name).copyToUTF8(label,sizeof(label));if(ImGui::InputText(L("Nom","Name"),label,sizeof(label))){auto next=t.document->session();next.tracks[index].name=label;edit(t,std::move(next),ui::Ls("Renommer la sous-piste","Rename subtrack"));}
                if(ImGui::MenuItem(L("Cadrer dans la timeline","Focus in timeline")))focusCinemaTrack(t,track.id);
                if(ImGui::MenuItem(L("Effets de la sous-piste","Subtrack effects"))){t.processing->selectInput(track.id);t.processing->showPanel(0);state_.setBottomPanelVisible(true);}
                if(track.midi&&ImGui::MenuItem(L("Choisir l'instrument…","Choose instrument…")))t.processing->chooseInstrument(track.id);
                if(ImGui::MenuItem(L("Automation","Automation"))){if(!t.automationRows.erase(track.id))t.automationRows.insert(track.id);}
                auto mute=track.mute,solo=track.solo;if(ImGui::MenuItem(L("Muet","Mute"),nullptr,mute))trackMix(track.id,!mute,solo);if(ImGui::MenuItem("Solo",nullptr,solo))trackMix(track.id,mute,!solo);
                if(ImGui::BeginMenu(L("Déplacer vers un bus","Move to bus"))){for(const auto& bus:s.tracks)if(bus.bus)if(ImGui::MenuItem(bus.name.c_str(),nullptr,track.parentId==bus.id))assignCinemaTrack(t,track.id,bus.id);ImGui::EndMenu();}
                if(ImGui::MenuItem(L("Supprimer le conteneur et ses clips","Delete container and clips")))queue("container","delete",track.id);
                ImGui::EndPopup();
            }
            if(io.MousePos.x>=left&&io.MousePos.x<right&&io.MousePos.y>=top&&io.MousePos.y<top+boxHeight)overContainer=true;
            const ImVec2 clipTop(left+dp(3),top+dp(25)),clipBottom(right-dp(3),top+dp(t.cinemaContentsVisible?101:36));
            if(clipBottom.x>clipTop.x){
                if(t.cinemaContentsVisible){cinemaDropTargets_.push_back({clipTop,clipBottom,index,t.framesPerPixel,timelineLeft,laneWidth});drawCinemaClipLane(t,index,clipTop,clipBottom,timelineLeft,timelineRight);}
                else{draw->PushClipRect(clipTop,clipBottom,true);for(const auto& clip:track.clips)drawClip(t,clip,index,clipTop,clipBottom,timelineLeft,timelineRight,accent);draw->PopClipRect();}
            }
            if(t.cinemaContentsVisible&&t.automationRows.contains(track.id)){const ImVec2 at(left+dp(3),top+dp(107));lanesMin_=ImVec2(timelineLeft,at.y);drawAutomation(t,index,at,ImVec2(right-dp(3),at.y+dp(84)));}
            ImGui::PopID();
        }
        if(collapsed)for(int index:children){const auto [a,b]=cinemaBounds(s.tracks[index],s.rate);const auto left=std::max(timelineLeft,xOf(a)),right=std::min(timelineRight,xOf(b));if(right>left)draw->AddRectFilled(ImVec2(left,origin.y+dp(10)),ImVec2(right,origin.y+dp(48)),theme::alpha(p.audio,.3f),dp(2));}
        if(!overContainer){
        ImGui::SetCursorScreenPos(laneTop);ImGui::InvisibleButton("##cinemaBusLane",ImVec2(laneWidth,rowHeight),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
        if(!overContainer&&ImGui::IsItemHovered()&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1))){t.cursor=frameAt(io.MousePos.x);t.activeGroup=busId;t.processing->selectInput(busId);}
        if(!overContainer&&ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)){t.cursor=frameAt(io.MousePos.x);addCinemaTrack(t,busId,false);}
        if(ImGui::IsItemClicked(1))ImGui::OpenPopup("##cinemaBusEmptyMenu");
        } // empty bus area only
        if(ImGui::BeginPopup("##cinemaBusEmptyMenu")){
            if(ImGui::MenuItem(L("Créer une sous-piste audio ici","Create audio subtrack here"))){addCinemaTrack(t,busId,false);}
            if(ImGui::MenuItem(L("Créer une sous-piste MIDI ici","Create MIDI subtrack here"))){addCinemaTrack(t,busId,true);}ImGui::EndPopup();
        }
        if(!overContainer&&ImGui::BeginDragDropTarget()){
            if(const auto* payload=ImGui::AcceptDragDropPayload(kFilePayload);payload&&payload->DataSize>1){const auto file=juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data),payload->DataSize-1));
                if(cinemaVideoFile(file))openCinemaVideo(file);else{t.cursor=frameAt(io.MousePos.x);const auto id=addCinemaTrack(t,busId,false,file.getFileNameWithoutExtension());importFile(t,file,t.document->session().trackIndex(id),t.cursor);}}
            ImGui::EndDragDropTarget();
        }
        const auto head=xOf(transportState().seconds*s.rate);if(head>=timelineLeft&&head<timelineRight)draw->AddLine(ImVec2(head,origin.y),ImVec2(head,laneBottom.y),p.accent,dp(1.5));
        ImGui::SetCursorScreenPos(ImVec2(origin.x,laneBottom.y));ImGui::Dummy(ImVec2(0,0));
        if(t.automationRows.contains(busId)&&busIndex>=0){const ImVec2 at(timelineLeft,ImGui::GetCursorScreenPos().y);lanesMin_=at;drawAutomation(t,busIndex,at,ImVec2(timelineRight,at.y+dp(90)));ImGui::SetCursorScreenPos(ImVec2(origin.x,at.y+dp(94)));ImGui::Dummy(ImVec2(0,0));}
        ImGui::PopID();
    }
    if(cinemaContainerGesture_){auto& gesture=*cinemaContainerGesture_;
        if(ImGui::IsMouseDragging(0)){
            auto next=gesture.before;const auto index=next.trackIndex(gesture.original.id);const auto [a,b]=cinemaBounds(gesture.original,s.rate);
            const auto delta=static_cast<std::int64_t>((io.MousePos.x-gesture.x)*t.framesPerPixel);
            if(index>=0){auto& tr=next.tracks[index];
                if(gesture.kind==0){cinemaPlace(tr,snap(a+delta),b-a,true,s.rate);for(const auto& bus:cinemaBusTargets_)if(io.MousePos.y>=bus.min.y&&io.MousePos.y<bus.max.y){gesture.destination=bus.id;break;}}
                else if(gesture.kind==1){const auto start=std::min(snap(a+delta),b-1);cinemaPlace(tr,start,b-start,false,s.rate);}
                else cinemaPlace(tr,a,std::max<std::int64_t>(1,snap(b+delta)-a),false,s.rate);
                t.document->replace(std::move(next));if(playingTab_==current_)this->player().setSession(t.document->shared());
            }
        }
        if(ImGui::IsMouseReleased(0)){
            auto next=t.document->session();const auto index=next.trackIndex(gesture.original.id);const bool changed=std::abs(io.MousePos.x-gesture.x)>dp(2)||gesture.destination!=gesture.original.parentId;
            const auto id=gesture.original.id,destination=gesture.destination;const bool route=destination!=gesture.original.parentId;
            if(index>=0&&gesture.kind==0)next.tracks[index].parentId=destination;
            if(changed)t.document->replace(gesture.before);cinemaContainerGesture_.reset();if(changed){edit(t,std::move(next),ui::Ls("Éditer le conteneur","Edit container"));if(route){syncProcessing(t);t.processing->routeInput(id,destination);}}
        }
    }
    if(cinemaGestureBefore_&&ImGui::IsMouseReleased(0)){auto next=t.document->session();t.document->replace(*cinemaGestureBefore_);cinemaGestureBefore_.reset();edit(t,std::move(next),ui::Ls("Rogner le clip","Trim clip"));cinemaGestureKind_=0;}
    const auto masterTop=ImGui::GetCursorScreenPos();if(ImGui::Button("MASTER",ImVec2(headerWidth-dp(8),0))){t.masterSelected=true;t.processing->selectMaster();t.processing->showPanel(0);state_.setBottomPanelVisible(true);}
    ImGui::SameLine();if(ImGui::SmallButton(L("Automation master","Master automation"))){if(!t.automationRows.erase(0))t.automationRows.insert(0);}
    if(t.automationRows.contains(0)){const ImVec2 at(timelineLeft,ImGui::GetCursorScreenPos().y);lanesMin_=at;drawAutomation(t,-1,at,ImVec2(timelineRight,at.y+dp(90)));ImGui::SetCursorScreenPos(ImVec2(masterTop.x,at.y+dp(94)));ImGui::Dummy(ImVec2(0,0));}
    ImGui::EndChild();
    double end=std::max(static_cast<double>(s.frames()),t.viewStart+t.cinemaVisibleSeconds*s.rate);for(const auto& track:s.tracks)if(!track.bus)end=std::max(end,static_cast<double>(cinemaBounds(track,s.rate).second));
    // The same native horizontal scrollbar as the Arrangement, in sample units.
    if(!s.picture.file.empty())end=std::max(end,(s.picture.start+s.picture.duration)*s.rate);
    end=std::max(end*1.05,t.cinemaVisibleSeconds*s.rate);
    auto scroll=static_cast<ImS64>(t.viewStart);const auto at=ImGui::GetCursorScreenPos();
    const auto barHeight=ImGui::GetStyle().ScrollbarSize;
    if(ImGui::ScrollbarEx(ImRect(ImVec2(timelineLeft,at.y),ImVec2(timelineRight,at.y+barHeight)),ImGui::GetID("##cinemaHorizontalScroll"),ImGuiAxis_X,&scroll,
        static_cast<ImS64>(t.cinemaVisibleSeconds*s.rate),static_cast<ImS64>(end))){t.viewStart=static_cast<double>(scroll);if(player().playing())cinemaFollowPaused_=true;}
    ImGui::Dummy(ImVec2(0,barHeight));
}
