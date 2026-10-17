void FoundryView::zoomCinema(Tab& t, double factor, double fraction)
{
    fraction=std::clamp(fraction,0.0,1.0);
    const auto anchor=t.viewStart+fraction*t.cinemaVisibleSeconds*t.document->session().rate;
    t.cinemaVisibleSeconds=std::clamp(t.cinemaVisibleSeconds/factor,.1,7200.0);
    t.viewStart=std::max(0.0,anchor-fraction*t.cinemaVisibleSeconds*t.document->session().rate);
}
void FoundryView::fitCinema(Tab& t)
{
    const auto& s=t.document->session();double end=0;
    for(const auto& tr:s.tracks)if(!tr.bus)end=std::max(end,cinemaBounds(tr,s.rate).second/s.rate);
    if(!s.picture.file.empty())end=std::max(end,s.picture.start+s.picture.duration);
    t.viewStart=0;t.cinemaVisibleSeconds=std::clamp(std::max(2.0,end)*1.05,.1,7200.0);
}
void FoundryView::drawCinemaToolbar(Tab& t)
{
    if(ui::iconButton(icon::add,L("Ajouter un bus ou une sous-piste", "Add a bus or subtrack")))ImGui::OpenPopup("##cinemaAdd");
    if(ImGui::BeginPopup("##cinemaAdd")){
        if(ImGui::MenuItem(L("Bus", "Bus")))addCinemaGroup(t,{});
        if(ImGui::MenuItem(L("Sous-piste audio", "Audio subtrack")))addCinemaTrack(t,t.activeGroup,false);
        if(ImGui::MenuItem(L("Sous-piste instrument MIDI", "MIDI instrument subtrack")))addCinemaTrack(t,t.activeGroup,true);
        ImGui::Separator();if(ImGui::MenuItem(L("Importer des sons…", "Import sounds…")))chooseImport();
        if(ImGui::MenuItem(L("Charger une vidéo…", "Load video…")))chooseCinemaVideo();
        ImGui::EndPopup();
    }
    ImGui::SameLine(0,dp(4));ImGui::TextDisabled("%s",L("Piste", "Track"));
    ui::toolbarSeparator();ImGui::TextDisabled("%s",L("Grille", "Grid"));ImGui::SameLine();
    const char* names[]={L("Libre", "Off"),L("Image", "Frame"),L("2 images", "2 frames"),L("5 images", "5 frames"),L("10 images", "10 frames")};
    constexpr int frames[]={1,1,2,5,10};int grid=t.cinemaSnap?1:0;
    if(t.cinemaSnap)for(int i=1;i<5;++i)if(t.cinemaGridFrames==frames[i])grid=i;
    ImGui::SetNextItemWidth(dp(84));if(ImGui::Combo("##cinemaGrid",&grid,names,5)){t.cinemaSnap=grid!=0;t.cinemaGridFrames=frames[grid];}
    ui::tooltip(L("Aimantation aux images ; Alt pour la désactiver pendant un geste", "Snap to video frames; hold Alt to bypass"));
    ui::toolbarSeparator();
    if(ui::iconButton(icon::zoomOut,L("Dézoomer (Ctrl+molette, -)", "Zoom out (Ctrl+wheel, -)")))zoomCinema(t,1/1.4);
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::zoomIn,L("Zoomer (Ctrl+molette, +)", "Zoom in (Ctrl+wheel, +)")))zoomCinema(t,1.4);
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::fullScreen,L("Tout voir", "Zoom to fit")))fitCinema(t);
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::forward,cinemaFollowPaused_?L("Reprendre le suivi de lecture", "Resume playback following"):L("Suivre la lecture", "Follow playback"),cinemaFollow_&&!cinemaFollowPaused_)){
        if(cinemaFollowPaused_)cinemaFollowPaused_=false;else cinemaFollow_=!cinemaFollow_;
    }
    ui::toolbarSeparator();
    ImGui::BeginDisabled(!t.clip);
    if(ui::iconButton(icon::cut,L("Scinder à la tête de lecture (S)", "Split at the playhead (S)"))){t.cursor=static_cast<std::int64_t>(transportState().seconds*t.document->session().rate);splitAtCursor(t);}
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::copy,L("Dupliquer (Ctrl+D)", "Duplicate (Ctrl+D)")))duplicateClip(t);
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::trash,L("Supprimer (Suppr)", "Delete (Del)")))remove(t);
    ImGui::EndDisabled();ui::toolbarSeparator();
    if(ui::iconButton(icon::undo,L("Annuler (Ctrl+Z)", "Undo (Ctrl+Z)")))undo();
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::redo,L("Rétablir (Ctrl+Y)", "Redo (Ctrl+Y)")))redo();
    ui::toolbarSeparator();
    if(ui::iconButton(icon::previous,L("Image précédente (flèche gauche)", "Previous frame (left arrow)"))){TransportSync::Update u;u.state=transportState();u.state.seconds=std::max(0.0,u.state.seconds-1/t.document->session().picture.fps);u.position=true;applyTransport(u);}
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::next,L("Image suivante (flèche droite)", "Next frame (right arrow)"))){TransportSync::Update u;u.state=transportState();u.state.seconds+=1/t.document->session().picture.fps;u.position=true;applyTransport(u);}
    ui::toolbarSeparator();
    if(ui::iconButton(icon::save,L("Enregistrer (Ctrl+S)", "Save (Ctrl+S)")))save(false);
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::exportFile,L("Exporter le mixage…", "Export mix…")))chooseExport();
    ImGui::SameLine(0,dp(2));if(ui::iconButton(icon::view,L("Panneaux et affichage", "Panels and view")))ImGui::OpenPopup("##cinemaView");
    if(ImGui::BeginPopup("##cinemaView")){
        ImGui::Checkbox(L("Prévisualisation vidéo", "Video preview"),&cinemaVideoVisible_);
        ImGui::Checkbox(L("Vignettes vidéo dans la timeline", "Video filmstrip in timeline"),&t.cinemaFilmstrip);
        ImGui::Checkbox(L("Afficher le contenu des sous-pistes", "Show subtrack contents"),&t.cinemaContentsVisible);
        auto tools=state_.bottomPanelVisible();if(ImGui::Checkbox(L("Périphériques et détails (Ctrl+J)", "Devices and details (Ctrl+J)"),&tools))state_.setBottomPanelVisible(tools);
        ImGui::Separator();if(ImGui::MenuItem(L("Réinitialiser la disposition", "Reset layout"))){cinemaResetLayout_=true;cinemaVideoVisible_=true;state_.setBottomPanelVisible(true);}
        ImGui::EndPopup();
    }
    if(!state_.bottomPanelVisible()){ImGui::SameLine();if(ui::iconButton(icon::dockBottom,L("Afficher le panneau du bas (Ctrl+J)", "Show the bottom panel (Ctrl+J)")))state_.setBottomPanelVisible(true);}
}
void FoundryView::drawCinema()
{
    if(tabs_.empty())newDocument();
    tickCinemaVideo();drawTabs();auto* current=tab();if(!current)return;auto& t=*current;
    if(playingTab_>=0&&!player().playing())playingTab_=-1;
    t.tracks.resize(t.document->session().tracks.size(),false);syncProcessing(t);
    const auto id=ImGui::GetID("CinemaWorkspaceV2");
    const auto timelineName=L("Montage###cinemaTimeline", "Timeline###cinemaTimeline");
    const auto videoName=L("Vidéo###cinemaPreview", "Video###cinemaPreview");
    const auto toolsName=L("Périphériques de la piste###cinemaTools", "Track devices###cinemaTools");
    const auto detailsName=L("Inspecteur de clip###cinemaDetails", "Clip inspector###cinemaDetails");
    const auto available=ImGui::GetContentRegionAvail();
    if(cinemaResetLayout_||!ImGui::DockBuilderGetNode(id)){
        cinemaResetLayout_=false;ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id,ImGui::GetCursorScreenPos());ImGui::DockBuilderSetNodeSize(id,available);
        ImGuiID main=id;const auto right=ImGui::DockBuilderSplitNode(main,ImGuiDir_Right,.26f,nullptr,&main);
        ImGuiID leftTop=main;const auto leftBottom=ImGui::DockBuilderSplitNode(leftTop,ImGuiDir_Down,.30f,nullptr,&leftTop);
        ImGuiID rightTop=right;const auto rightBottom=ImGui::DockBuilderSplitNode(rightTop,ImGuiDir_Down,.30f,nullptr,&rightTop);
        ImGui::DockBuilderDockWindow(timelineName,leftTop);ImGui::DockBuilderDockWindow(videoName,rightTop);
        ImGui::DockBuilderDockWindow(toolsName,leftBottom);ImGui::DockBuilderDockWindow(detailsName,rightBottom);
        ImGui::DockBuilderFinish(id);
    }
    ImGui::DockSpace(id,available,ImGuiDockNodeFlags_None);
    // Ordinary dock windows: their tabs can be dragged, resized, tabbed together,
    // or detached. Stable IDs retain the user's layout independently of Studio.
    constexpr auto flags=ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoFocusOnAppearing;
    if(ImGui::Begin(timelineName,nullptr,flags|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
        drawCinemaToolbar(t);drawCinemaTracks(t,ImGui::GetContentRegionAvail());
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput){
            handleKeys(t);
            const auto direction=ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)?1:ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)?-1:0;
            if(direction&&!ImGui::GetIO().KeyCtrl){TransportSync::Update u;u.state=transportState();u.state.seconds=std::max(0.0,u.state.seconds+direction/t.document->session().picture.fps);u.position=true;applyTransport(u);}
        }
    }ImGui::End();
    if(cinemaVideoVisible_){
        if(ImGui::Begin(videoName,&cinemaVideoVisible_,flags))drawCinemaVideo(t,ImVec2(0,0));
        ImGui::End();
    }
    cinemaListTargets_.clear();
    if(state_.bottomPanelVisible()){
        bool toolsOpen=true;
        if(ImGui::Begin(toolsName,&toolsOpen,flags))t.processing->draw([&]{drawMidiEditor(t);},true,[&]{state_.setBottomPanelVisible(false);},t.activeGroup?[&]{drawCinemaSubtracks(t);}:std::function<void()>{});
        ImGui::End();
        if(!toolsOpen)state_.setBottomPanelVisible(false);
        if(state_.bottomPanelVisible()){
            if(ImGui::Begin(detailsName,nullptr,flags))drawSide(t,ImVec2(0,0));
            ImGui::End();
        }
    }else t.processing->draw({},false);
    commitProcessing(t);
    if(player().playing()||(t.video&&t.video->snapshot().loading))host_.wake(2);
    if(cinemaPending_.isObject()){const auto request=cinemaPending_;cinemaPending_=juce::var();(void)command(request);}
}
