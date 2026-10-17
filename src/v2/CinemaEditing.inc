// Cinema uses the same clip document, editor, MIDI instruments, automation and processing rack.
namespace {
juce::String cinemaTime(double seconds,double fps){
    if(std::abs(fps-29.97)<.02){const auto c=timecode::fromSeconds(seconds,2);return juce::String::formatted("%02d:%02d:%02d:%02d",c.hours,c.minutes,c.seconds,c.frames);}
    const auto nominal=std::max(1,static_cast<int>(std::round(fps)));
    const auto frame=static_cast<std::int64_t>(std::floor(std::max(0.0,seconds)*fps+1.0e-7));
    return juce::String::formatted("%02d:%02d:%02d:%02d",static_cast<int>(frame/(nominal*3600)%24),static_cast<int>(frame/(nominal*60)%60),static_cast<int>(frame/nominal%60),static_cast<int>(frame%nominal));
}
std::pair<std::int64_t,std::int64_t> cinemaBounds(const engine::foundry::Track& track,double rate){
    auto start=std::max<std::int64_t>(0,track.containerStart);
    auto end=start+std::max<std::int64_t>(1,track.containerLength);
    if(track.containerLength<=0){
        if(track.clips.empty())end=start+static_cast<std::int64_t>(rate*2);
        else{start=track.clips.front().start;end=track.clips.front().end();}
    }
    for(const auto& clip:track.clips){start=std::min(start,clip.start);end=std::max(end,clip.end());}
    return {start,std::max(start+1,end)};
}
void cinemaPlace(engine::foundry::Track& track,std::int64_t start,std::int64_t length,bool move,double rate){
    const auto [oldStart,oldEnd]=cinemaBounds(track,rate);
    start=std::max<std::int64_t>(0,start);length=std::max<std::int64_t>(1,length);
    if(move){const auto delta=start-oldStart;for(auto& clip:track.clips)clip.start+=delta;
        for(auto& lane:track.automation)for(auto& point:lane.points)point.frame=std::max<std::int64_t>(0,point.frame+delta);
    }else for(const auto& clip:track.clips){const auto end=std::max(start+length,clip.end());start=std::min(start,clip.start);length=end-start;}
    track.containerStart=start;track.containerLength=length;
}
bool cinemaVideoFile(const juce::File& file){return file.hasFileExtension("mp4;mov;m4v;avi;wmv;mkv;webm;mpeg;mpg;mxf;ts;mts;m2ts;m2v;vob;ogv;flv;3gp;3g2;dv;nut;rm;rmvb");}
}
void FoundryView::ensureCinema(Tab& t){
    auto next=t.document->session();const bool wasCinema=next.cinema;next.cinema=true;
    std::uint64_t group=0;
    for(const auto& track:next.tracks)if(track.virtualGroup){group=track.id;break;}
    bool made=false;
    if(!group&&!wasCinema){Track bus;bus.id=next.newId();bus.bus=true;bus.virtualGroup=true;bus.name="Bus 1";group=bus.id;next.tracks.insert(next.tracks.begin(),std::move(bus));made=true;}
    if(made)for(auto& track:next.tracks)if(!track.bus)track.parentId=group;
    if(!wasCinema&&next.frames()<=0)next.span=static_cast<std::int64_t>(60*next.rate);
    t.activeGroup=group;if(!wasCinema||made)t.document->replace(std::move(next));
    t.videoTextureKey="cinema-video-"+juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(t.document.get()));
    syncProcessing(t);
    if(made)for(const auto& track:t.document->session().tracks)if(track.parentId) t.processing->routeInput(track.id,track.parentId);
}
std::uint64_t FoundryView::addCinemaGroup(Tab& t,const juce::String& name){
    auto next=t.document->session();Track bus;bus.id=next.newId();bus.bus=true;bus.virtualGroup=true;
    bus.name=name.isNotEmpty()?name.toStdString():"Bus";const auto id=bus.id;
    next.tracks.push_back(std::move(bus));edit(t,std::move(next),ui::Ls("Créer un bus","Create bus"));
    syncProcessing(t);t.activeGroup=id;t.processing->selectInput(id);return id;
}
std::uint64_t FoundryView::addCinemaTrack(Tab& t,std::uint64_t group,bool midi,const juce::String& name){
    auto next=t.document->session();
    const auto parent=next.trackIndex(group);if(group&&(parent<0||!next.tracks[parent].virtualGroup))return 0;
    Track track;track.id=next.newId();track.parentId=group;track.midi=midi;
    track.name=name.isNotEmpty()?name.toStdString():midi?"Instrument":"Audio";
    track.containerStart=t.cursor;track.containerLength=static_cast<std::int64_t>(next.rate*2);
    const auto id=track.id;next.tracks.push_back(std::move(track));
    edit(t,std::move(next),ui::Ls("Créer une sous-piste","Create subtrack"));syncProcessing(t);
    t.processing->routeInput(id,group);t.processing->selectInput(id);t.activeGroup=group;
    pickTrack(t,t.document->session().trackIndex(id),false);return id;
}
bool FoundryView::assignCinemaTrack(Tab& t,std::uint64_t id,std::uint64_t group){
    auto next=t.document->session();const auto index=next.trackIndex(id),parent=next.trackIndex(group);
    if(index<0||next.tracks[index].bus||(group&&(parent<0||!next.tracks[parent].virtualGroup)))return false;
    next.tracks[index].parentId=group;edit(t,std::move(next),ui::Ls("Déplacer la sous-piste","Move subtrack"));
    syncProcessing(t);t.processing->routeInput(id,group);t.activeGroup=group;return true;
}
bool FoundryView::openCinemaVideo(const juce::File& file){
    if(!file.existsAsFile()){report(ui::Ls("Vidéo introuvable : ","Video missing: ")+file.getFullPathName(),true);return false;}
    if(!tab())newDocument();
    auto& t=*tab();auto next=t.document->session();next.picture.file=file.getFullPathName().toStdString();
    next.picture.sourceIn=0;next.picture.duration=0;
    edit(t,std::move(next),ui::Ls("Charger une vidéo de référence","Load reference video"));
    releaseCinemaThumbnails(t);cinemaVideoVisible_=true;
    t.video=std::make_unique<CinemaVideo>();t.videoRevision=0;
    t.video->request(file,0);importCinemaAudio(true);return true;
}
void FoundryView::chooseCinemaClipAudio(Tab& t,std::uint64_t track,std::int64_t at){
    const auto identity=t.cinemaIdentity;
    chooser_=std::make_unique<juce::FileChooser>(ui::Ls("Importer un son dans la sous-piste", "Import sound into subtrack"),juce::File(),"*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.wma");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [this,identity,track,at](const juce::FileChooser& chooser){
            const auto file=chooser.getResult();if(file==juce::File())return;
            for(auto& target:tabs_)if(target.cinemaIdentity==identity){
                const auto index=target.document->session().trackIndex(track);if(index<0)return;
                if(importFile(target,file,index,at)){
                    const auto& clips=target.document->session().tracks[index].clips;
                    if(!clips.empty())target.clip=std::max_element(clips.begin(),clips.end(),[](const auto& a,const auto& b){return a.id<b.id;})->id;
                    target.detailsFocus=true;
                }break;
            }
        });
}
void FoundryView::createCinemaAudioClip(Tab& t,int track,std::int64_t at){
    auto next=t.document->session();if(track<0||track>=static_cast<int>(next.tracks.size())||next.tracks[track].midi||next.tracks[track].bus)return;
    Clip clip;clip.id=next.newId();clip.start=at;clip.length=static_cast<std::int64_t>(next.rate*60/std::max(1.0,next.tempo)*4);
    clip.name="Audio";clip.sound=std::make_shared<Sound>(2,clip.length,next.rate);clip.edited=true;t.clip=clip.id;
    next.tracks[track].clips.push_back(std::move(clip));edit(t,std::move(next),ui::Ls("Créer un clip audio", "Create audio clip"));
    pickTrack(t,track,false);t.detailsFocus=true;
}
void FoundryView::chooseCinemaVideo(){
    chooser_=std::make_unique<juce::FileChooser>(ui::Ls("Charger une vidéo de référence","Load reference video"),juce::File(),
        "*.mp4;*.mov;*.m4v;*.avi;*.wmv;*.mkv;*.webm;*.mpeg;*.mpg;*.mxf;*.ts;*.mts;*.m2ts;*.m2v;*.vob;*.ogv;*.flv;*.3gp;*.3g2;*.dv;*.nut;*.rm;*.rmvb");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& f){if(f.getResult()!=juce::File())openCinemaVideo(f.getResult());});
}
void FoundryView::tickCinemaVideo(){
    if(!cinema_)return;
    pollCinemaAudio();
    auto* t=tab();if(!t)return;
    const auto& p=t->document->session().picture;
    if(p.file.empty()){if(t->thumbnailDecoder)releaseCinemaThumbnails(*t);if(t->video){t->video.reset();host_.releaseTexture(t->videoTextureKey);t->videoTexture=0;t->videoRevision=0;}return;}
    if(!t->video)t->video=std::make_unique<CinemaVideo>();
    const auto time=transportState().seconds-p.start+p.sourceIn;
    t->video->request(juce::File(fromUtf8(p.file)),time);
    const auto frame=t->video->snapshot();
    if(frame.duration>0&&p.duration<=0&&frame.error.isEmpty()){
        auto next=t->document->session();next.picture.duration=std::max(0.0,frame.duration-next.picture.sourceIn);
        next.picture.fps=frame.fps;
        next.span=static_cast<std::int64_t>((next.picture.start+next.picture.duration)*next.rate);
        t->document->replace(std::move(next));
        if(playingTab_==current_)this->player().setSession(t->document->shared());
    }
}
bool FoundryView::dropCinemaFiles(const juce::StringArray& files,float x,float y){
    bool any=false;
    for(const auto& path:files){const juce::File file(path);
        if(cinemaVideoFile(file)){any=openCinemaVideo(file)||any;continue;}
        if(file.hasFileExtension("cycinema;cyfoundry")){any=open(file)||any;continue;}
        if(!tab())newDocument();auto& t=*tab();int track=-1;auto at=t.cursor;
        for(const auto& target:cinemaDropTargets_)if(x>=target.min.x&&x<target.max.x&&y>=target.min.y&&y<target.max.y){
            track=target.track;at=std::max<std::int64_t>(0,static_cast<std::int64_t>(t.viewStart+(x-target.timelineX)*target.framesPerPixel));break;}
        if(track<0){for(const auto& bus:cinemaBusTargets_)if(x>=bus.min.x&&x<bus.max.x&&y>=bus.min.y&&y<bus.max.y){
            t.activeGroup=bus.id;t.cursor=std::max<std::int64_t>(0,static_cast<std::int64_t>(t.viewStart+(x-bus.min.x)*t.framesPerPixel));at=t.cursor;break;}}
        if(track<0){const auto id=addCinemaTrack(t,t.activeGroup,false,file.getFileNameWithoutExtension());track=t.document->session().trackIndex(id);}
        any=importFile(t,file,track,at)||any;
    }return any;
}
void FoundryView::drawCinemaVideo(Tab& t,ImVec2 size){
    const auto retained=t.document->shared();const auto& picture=retained->picture;
    ImGui::BeginChild("##cinemaPicture",size,ImGuiChildFlags_Borders);
    if(ImGui::Button(L("Charger une vidéo…","Load video…")))chooseCinemaVideo();
    ImGui::SameLine();if(ImGui::Button(L("Sans vidéo","No video"))){auto next=*retained;next.picture.file.clear();edit(t,std::move(next),ui::Ls("Retirer la vidéo","Remove video"));}
    if(!picture.file.empty()){
        if(ImGui::Button(L("Importer la bande son comme piste…","Import soundtrack as track…")))importCinemaAudio();
        if(cinemaAudioTask_&&!cinemaAudioTask_->done)ImGui::TextDisabled("%s",L("Décodage audio…","Decoding audio…"));
        else if(cinemaAudioTask_&&cinemaAudioTask_->error.isNotEmpty())ImGui::TextWrapped("%s",cinemaAudioTask_->error.toRawUTF8());
    }
    const auto fps=picture.fps;
    ImGui::Text("%s",cinemaTime(transportState().seconds,fps).toRawUTF8());
    if(!picture.file.empty())ImGui::TextWrapped("%s",juce::File(fromUtf8(picture.file)).getFileName().toRawUTF8());
    if(t.video){const auto info=t.video->snapshot();if(info.codec.isNotEmpty())ImGui::TextDisabled("%s · %s",info.backend.toRawUTF8(),info.codec.toRawUTF8());}
    const auto area=ImGui::GetContentRegionAvail();const auto box=ImVec2(std::max(1.f,area.x),std::max(dp(40),std::min(std::max(dp(40),area.y-dp(115)),area.x*9/16)));
    const auto origin=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,ImVec2(origin.x+box.x,origin.y+box.y),IM_COL32(0,0,0,255));
    bool drawn=false;
    if(t.video){const auto frame=t.video->snapshot();
        if(frame.revision!=t.videoRevision&&frame.image.isValid()){
            t.videoTexture=host_.updateTexture(t.videoTextureKey,frame.image);t.videoRevision=frame.revision;
        }
        const auto local=transportState().seconds-picture.start;
        if(t.videoTexture&&frame.image.isValid()&&local>=0&&(picture.duration<=0||local<picture.duration)){
            const auto scale=std::min(box.x/frame.image.getWidth(),box.y/frame.image.getHeight());
            const ImVec2 extent(frame.image.getWidth()*scale,frame.image.getHeight()*scale);
            const ImVec2 top(origin.x+(box.x-extent.x)*.5f,origin.y+(box.y-extent.y)*.5f);
            draw->AddImage(static_cast<ImTextureID>(t.videoTexture),top,ImVec2(top.x+extent.x,top.y+extent.y));drawn=true;
        }
        if(frame.error.isNotEmpty())ImGui::TextWrapped("%s",frame.error.toRawUTF8());
    }
    if(!drawn)draw->AddText(ImVec2(origin.x+dp(10),origin.y+dp(10)),theme::palette().textDim,
        picture.file.empty()?L("Transport externe / vidéo facultative","External transport / optional video"):L("Chargement / hors du plan","Loading / outside picture"));
    ImGui::SetCursorScreenPos(origin);ImGui::Dummy(box);
    if(ImGui::CollapsingHeader(L("Réglages vidéo", "Video settings"))){
    auto change=[&](const char* label,double& value){ImGui::SetNextItemWidth(dp(110));return ImGui::InputDouble(label,&value,1,10,"%.3f");};
    double start=picture.start,in=picture.sourceIn,duration=picture.duration,frameRate=picture.fps;
    bool changed=change(L("Début dans le projet (s)","Timeline start (s)"),start);
    changed=change(L("Point d'entrée vidéo (s)","Video in point (s)"),in)||changed;
    changed=change(L("Durée du plan (s)","Picture length (s)"),duration)||changed;
    changed=change(L("Images / seconde","Frames / second"),frameRate)||changed;
    if(changed){auto next=*retained;next.picture.start=std::max(0.0,start);next.picture.sourceIn=std::max(0.0,in);
        next.picture.duration=std::max(0.0,duration);next.picture.fps=std::clamp(frameRate,1.0,240.0);
        if(!next.picture.file.empty())next.span=static_cast<std::int64_t>((next.picture.start+next.picture.duration)*next.rate);
        edit(t,std::move(next),ui::Ls("Réglages image","Picture settings"));}
    }
    ImGui::TextWrapped("%s",L("Le transport Cinéma peut être relié à un logiciel externe avec SYNC, même sans vidéo locale.",
        "Connect the Cinema transport to external software with SYNC, even without a local video."));
    if(ImGui::Button(L("Image précédente","Previous frame"))){TransportSync::Update u;u.state=transportState();u.state.seconds=std::max(0.0,u.state.seconds-1/fps);u.position=true;applyTransport(u);}
    ImGui::SameLine();if(ImGui::Button(L("Image suivante","Next frame"))){TransportSync::Update u;u.state=transportState();u.state.seconds+=1/fps;u.position=true;applyTransport(u);}
    ImGui::EndChild();
}
#include "CinemaLayout.inc"
#include "CinemaFilmstrip.inc"

#include "CinemaTimeline.inc"

juce::var FoundryView::cinemaCommand(const juce::var& request){
    const auto action=request.getProperty("action",{}).toString();
    if(!tab())newDocument();auto& t=*tab();syncProcessing(t);
    auto id=[&](const char* key,std::uint64_t fallback){return static_cast<std::uint64_t>(static_cast<juce::int64>(request.getProperty(key,static_cast<juce::int64>(fallback))));};
    if(action=="video_audio")importCinemaAudio();
    else if(action=="video"){
        if(request.hasProperty("file")&&!openCinemaVideo(juce::File(request.getProperty("file",{}).toString())))return state_.status;
        auto next=t.document->session();auto& p=next.picture;
        if(static_cast<bool>(request.getProperty("remove",false)))p.file.clear();
        p.start=std::max(0.0,number(request,"start",p.start));p.sourceIn=std::max(0.0,number(request,"sourceIn",p.sourceIn));
        p.duration=std::max(0.0,number(request,"duration",p.duration));p.fps=std::clamp(number(request,"fps",p.fps),1.0,240.0);
        if(p.duration>0)next.span=static_cast<std::int64_t>((p.start+p.duration)*next.rate);
        edit(t,std::move(next),ui::Ls("Réglages vidéo","Video settings"));
    }else if(action=="group"){
        const auto op=request.getProperty("op","add").toString();const auto group=id("id",t.activeGroup);
        if(op=="add")addCinemaGroup(t,request.getProperty("name","Bus").toString());
        else if(op=="rename"){
            auto next=t.document->session();const auto index=next.trackIndex(group);if(index<0||!next.tracks[index].virtualGroup)return juce::String("unknown virtual track");
            next.tracks[index].name=request.getProperty("name","Bus").toString().toStdString();edit(t,std::move(next),"Rename virtual track");
        }else if(op=="remove"){
            auto next=t.document->session();const auto index=next.trackIndex(group);if(index<0||!next.tracks[index].virtualGroup)return juce::String("unknown virtual track");
            for(auto& track:next.tracks)if(track.parentId==group){track.parentId=0;t.processing->routeInput(track.id,0);}
            next=engine::foundry::withoutTrack(next,index);edit(t,std::move(next),ui::Ls("Retirer le bus","Remove bus"));syncProcessing(t);t.activeGroup=0;
        }else if(op=="select"){const auto index=t.document->session().trackIndex(group);if(index<0||!t.document->session().tracks[index].bus)return juce::String("unknown bus");t.activeGroup=group;t.processing->selectInput(group);t.processing->showPanel(5);state_.setBottomPanelVisible(true);}
        else return juce::String("group op: add, rename, select");
    }else if(action=="add_subtrack"||action=="add_track"){
        const auto created=addCinemaTrack(t,id("groupId",t.activeGroup),request.getProperty("type","audio").toString()=="midi",request.getProperty("name",{}).toString());
        if(!created)return juce::String("unknown bus");
        if(request.hasProperty("at")||request.hasProperty("length")){
            const auto& session=t.document->session();const auto factor=static_cast<bool>(request.getProperty("seconds",false))?session.rate:1.0;
            const auto index=session.trackIndex(created);const auto [start,end]=cinemaBounds(session.tracks[index],session.rate);
            changeCinemaContainer(t,created,static_cast<std::int64_t>(number(request,"at",start/factor)*factor),static_cast<std::int64_t>(number(request,"length",(end-start)/factor)*factor),true);
        }
    }else if(action=="container"){
        const auto trackId=id("id",id("trackId",0));const auto& session=t.document->session();const auto index=session.trackIndex(trackId);
        if(index<0||session.tracks[index].bus)return juce::String("unknown subtrack container");
        const auto op=request.getProperty("op","focus").toString();const auto [start,end]=cinemaBounds(session.tracks[index],session.rate);
        if(op=="focus"||op=="select"){
            t.activeGroup=session.tracks[index].parentId;pickTrack(t,index,false);if(op=="focus")focusCinemaTrack(t,trackId);
            state_.setBottomPanelVisible(true);t.processing->showPanel(5);
        }else if(op=="move"||op=="resize"){
            const auto factor=static_cast<bool>(request.getProperty("seconds",false))?session.rate:1.0;
            if(!changeCinemaContainer(t,trackId,static_cast<std::int64_t>(number(request,"at",start/factor)*factor),static_cast<std::int64_t>(number(request,"length",(end-start)/factor)*factor),op=="move"))return juce::String("invalid container");
            if(request.hasProperty("groupId"))assignCinemaTrack(t,trackId,id("groupId",0));
        }else if(op=="rename"){auto next=session;next.tracks[index].name=request.getProperty("name","Sous-piste").toString().toStdString();edit(t,std::move(next),ui::Ls("Renommer la sous-piste","Rename subtrack"));}
        else if(op=="delete"){auto next=engine::foundry::withoutTrack(session,index);edit(t,std::move(next),ui::Ls("Supprimer le conteneur","Delete container"));}
        else return juce::String("container op: focus, select, move, resize, rename, delete");
    }else if(action=="assign"){
        if(!assignCinemaTrack(t,id("trackId",0),id("groupId",t.activeGroup)))return juce::String("invalid subtrack or virtual track");
    }else if(action=="view"){
        t.viewStart=std::max(0.0,number(request,"start",t.viewStart/t.document->session().rate))*t.document->session().rate;
        t.cinemaVisibleSeconds=std::clamp(number(request,"seconds",t.cinemaVisibleSeconds),.1,7200.0);
        t.cinemaSnap=static_cast<bool>(request.getProperty("snap",t.cinemaSnap));
        t.cinemaFilmstrip=static_cast<bool>(request.getProperty("filmstrip",t.cinemaFilmstrip));
        if(request.hasProperty("zoom"))zoomCinema(t,std::clamp(number(request,"zoom",1),.01,100.0));
        if(static_cast<bool>(request.getProperty("fit",false)))fitCinema(t);
        if(static_cast<bool>(request.getProperty("resetLayout",false)))cinemaResetLayout_=true;
        t.cinemaContentsVisible=static_cast<bool>(request.getProperty("contentsVisible",t.cinemaContentsVisible));
        if(request.hasProperty("length")){auto next=t.document->session();next.span=static_cast<std::int64_t>(std::max(0.0,number(request,"length",60))*next.rate);edit(t,std::move(next),"Project length");}
    }
    tickCinemaVideo();commitProcessing(t,true);
    juce::var status(new juce::DynamicObject());status.getDynamicObject()->setProperty("action","status");return command(status);
}

void FoundryView::importCinemaAudio(const bool automatic){
    auto* t=tab();if(!t||t->document->session().picture.file.empty())return;
    const juce::File source(fromUtf8(t->document->session().picture.file));
    for(const auto& pending:cinemaAudioTasks_)if(!pending->done&&pending->documentIdentity==t->cinemaIdentity&&pending->source==source)return;
    auto task=std::make_shared<CinemaAudioTask>();task->source=source;task->automatic=automatic;
    task->target=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CyDAW Cinema").getChildFile(juce::Uuid().toString()+".wav");
    task->documentIdentity=t->cinemaIdentity;cinemaAudioTask_=task;cinemaAudioTasks_.push_back(task);
    cinemaAudioWorker_.addJob([task]{
        task->target.getParentDirectory().createDirectory();
        const auto result=CinemaVideo::extractAudio(task->source,task->target,[]{auto* job=juce::ThreadPoolJob::getCurrentThreadPoolJob();return job&&job->shouldExit();},&task->hasAudio);
        if(result.failed()){task->error=result.getErrorMessage();task->target.deleteFile();}
        task->done.store(true,std::memory_order_release);
    });
}
void FoundryView::pollCinemaAudio(){
    for(const auto& task:cinemaAudioTasks_){
        if(!task->done.load(std::memory_order_acquire)||task->consumed)continue;
        task->consumed=true;
        if(task->error.isNotEmpty()){report(task->error,true);task->target.deleteFile();continue;}
        if(!task->hasAudio){if(!task->automatic)report(ui::Ls("Cette vidéo ne contient pas de piste audio.","This video has no audio track."),false);task->target.deleteFile();continue;}
        for(auto& t:tabs_)if(t.cinemaIdentity==task->documentIdentity&&juce::File(fromUtf8(t.document->session().picture.file))==task->source){
            const auto picture=t.document->session().picture;
            const auto group=addCinemaGroup(t,ui::Ls("Bande son vidéo","Video soundtrack"));
            const auto id=addCinemaTrack(t,group,false,ui::Ls("Guide vidéo","Video guide"));
            const auto index=t.document->session().trackIndex(id);
            if(index>=0&&importFile(t,task->target,index,static_cast<std::int64_t>(picture.start*t.document->session().rate))){
                auto next=t.document->session();auto& clips=next.tracks[index].clips;
                if(!clips.empty()){
                    auto& clip=clips.back();clip.name=task->source.getFileNameWithoutExtension().toStdString();clip.edited=true;clip.file.clear();
                    clip.offset=0;clip.length=clip.sound->frames(); // Keep the entire soundtrack, independently of picture trims.
                }
                edit(t,std::move(next),ui::Ls("Bande son de référence","Reference soundtrack"));
            }break;
        }
        task->target.deleteFile();
    }
    std::erase_if(cinemaAudioTasks_,[](const auto& task){return task->consumed;});
}
