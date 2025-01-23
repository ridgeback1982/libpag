#pragma once

#include "nlohmann/json.hpp"
using json = nlohmann::json;


namespace movie {
    class Location {
    public:
        float w = 0;
        float h = 0;
        float center_x = 0;
        float center_y = 0;
        static void from_json(const json& j, Location& l) {
            if (j.contains("w"))
                j.at("w").get_to(l.w);
            if (j.contains("h"))
                j.at("h").get_to(l.h);
            j.at("center_x").get_to(l.center_x);
            j.at("center_y").get_to(l.center_y);
        }
    };
    static void from_json(const json& j, Location& l) {
        Location::from_json(j, l);
    }

    class VideoContent {
    public:
        std::string path;
        Location location;
        int mixVolume = 0;
        bool removeGreen = false;
        bool loop = false;
        float speed = 1.0f;
        int cutFrom = 0;
        int cutTo = 0;
        static void from_json(const json& j, VideoContent& v) {
            j.at("path").get_to(v.path);
            j.at("location").get_to(v.location);
            if (j.contains("mixVolume"))
                j.at("mixVolume").get_to(v.mixVolume);
            if (j.contains("removeGreen"))
                j.at("removeGreen").get_to(v.removeGreen);
            if (j.contains("loop"))
                j.at("loop").get_to(v.loop);
            if (j.contains("speed"))
                j.at("speed").get_to(v.speed);
            if (j.contains("cutFrom") && j.at("cutFrom").is_number())
                j.at("cutFrom").get_to(v.cutFrom);
            if (j.contains("cutTo") && j.at("cutTo").is_number())
                j.at("cutTo").get_to(v.cutTo);
        }

        int init(const std::string& tmpDir);
        int width() const { return _width; }
        int height() const { return _height; }
        int fps() const { return _fps; }
        std::string localPath() const { return _localPath; }
    private:
        int _width = 0;
        int _height = 0;
        int _fps = 0;
        std::string _localPath;
    };
    void from_json(const json& j, VideoContent& v) {
        VideoContent::from_json(j, v);
    }

    class AudioContent {
    public:
        std::string path;
        int mixVolume = 0;
        bool loop = false;
        float speed = 1.0f;
        int cutFrom = 0;
        int cutTo = 0;
        bool enhance = false;
        static void from_json(const json& j, AudioContent& a) {
            j.at("path").get_to(a.path);
            if (j.contains("mixVolume"))
                j.at("mixVolume").get_to(a.mixVolume);
            if (j.contains("loop"))
                j.at("loop").get_to(a.loop);
            if (j.contains("speed"))
                j.at("speed").get_to(a.speed);
            if (j.contains("cutFrom") && j.at("cutFrom").is_number())
                j.at("cutFrom").get_to(a.cutFrom);
            if (j.contains("cutTo") && j.at("cutTo").is_number())
                j.at("cutTo").get_to(a.cutTo);
            if (j.contains("enhance"))
                j.at("enhance").get_to(a.enhance);
        }

        int init(const std::string& tmpDir);
        std::string localPath() const { return _localPath; }
    private:
        std::string _localPath;
    };
    void from_json(const json& j, AudioContent& a) {
        AudioContent::from_json(j, a);
    }

    class Sentence {
    public:
        std::string text;
        int begin_time = 0;
        int end_time = 0;
        static void from_json(const json& j, Sentence& s) {
            j.at("text").get_to(s.text);
            j.at("begin_time").get_to(s.begin_time);
            j.at("end_time").get_to(s.end_time);
        }
    };
    void from_json(const json& j, Sentence& s) {
        Sentence::from_json(j, s);
    }

    class TitileContent {
    public:
        std::vector<Sentence> sentences;    //case of subtitle
        std::string text;                   //case of title
        Location location;
        float fontSize = 0.0f;
        std::string fontFamilyName;
        std::string textColor;
        std::string stroke;
        static void from_json(const json& j, TitileContent& t) {
            if (j.contains("sentences"))
                j.at("sentences").get_to(t.sentences);
            if (j.contains("text"))
                j.at("text").get_to(t.text);
            j.at("location").get_to(t.location);
            if (j.contains("fontSize"))
                j.at("fontSize").get_to(t.fontSize);
            if (j.contains("fontFamilyName"))
                j.at("fontFamilyName").get_to(t.fontFamilyName);
            if (j.contains("textColor"))
                j.at("textColor").get_to(t.textColor);
            if (j.contains("stroke"))
                j.at("stroke").get_to(t.stroke);
        }
    };
    void from_json(const json& j, TitileContent& t) {
        TitileContent::from_json(j, t);
    }

    class ImageContent {
    public:
        std::string path;
        Location location;
        static void from_json(const json& j, ImageContent& i) {
            j.at("path").get_to(i.path);
            j.at("location").get_to(i.location);
        }

        int init(const std::string& tmpDir);
        int width() const { return _width; }
        int height() const { return _height; }
        std::string localPath() const { return _localPath; }

    private:
        int _width = 0;
        int _height = 0;
        std::string _localPath;
    };
    void from_json(const json& j, ImageContent& i) {
        ImageContent::from_json(j, i);
    }

    class VerticalScope {
    public:
        float top = 0.2;
        float bottom = 0.8;
        static void from_json(const json& j, VerticalScope& v) {
            j.at("top").get_to(v.top);
            j.at("bottom").get_to(v.bottom);
        }
    };
    static void from_json(const json& j, VerticalScope& v) {
        VerticalScope::from_json(j, v);
    }

    class HorizontalScope {
    public:
        float left = 0.3;
        float right = 0.7;
        static void from_json(const json& j, HorizontalScope& h) {
            j.at("left").get_to(h.left);
            j.at("right").get_to(h.right);
        }
    };
    static void from_json(const json& j, HorizontalScope& h) {
        HorizontalScope::from_json(j, h);
    }

    class ArticleContent {
    public:
        std::string text;
        VerticalScope verticalVisibleScope;         //垂直方向可见区域
        HorizontalScope horizontalVisibleScope;     //水平方向可见区域
        float verticalSpacing = 0.0f;       //0.0 - 5.0, times of fontSize
        float horizontalSpacing = 0.0f;     //0.0 - 1.0, times of fontSize
        float paragraphSpacing = 0.0f;      //0.0 - 5.0, times of fontSize
        float startPosition = 0.5f;         //0.0 - 1.0, times of height(e.g. 1280), 0 is bottom, 1 is top
        float speed = 0.2f;                 //0.0 - 1.0, times of height(e.g. 1280) per second
        bool indented = true;               //indented(缩进) or not
        float fontSize = 0.05f;
        std::string fontFamilyName;
        std::string textColor;
        std::string stroke;
        static void from_json(const json& j, ArticleContent& a) {
            j.at("text").get_to(a.text);
            if (j.contains("verticalVisibleScope"))
                j.at("verticalVisibleScope").get_to(a.verticalVisibleScope);
            if (j.contains("horizontalVisibleScope"))
                j.at("horizontalVisibleScope").get_to(a.horizontalVisibleScope);
            if (j.contains("verticalSpacing"))
                j.at("verticalSpacing").get_to(a.verticalSpacing);
            if (j.contains("horizontalSpacing"))
                j.at("horizontalSpacing").get_to(a.horizontalSpacing);
            if (j.contains("paragraphSpacing"))
                j.at("paragraphSpacing").get_to(a.paragraphSpacing);
            if (j.contains("startPosition"))
                j.at("startPosition").get_to(a.startPosition);
            if (j.contains("speed"))
                j.at("speed").get_to(a.speed);
            if (j.contains("indented"))
                j.at("indented").get_to(a.indented);
            if (j.contains("fontSize"))
                j.at("fontSize").get_to(a.fontSize);
            if (j.contains("fontFamilyName"))
                j.at("fontFamilyName").get_to(a.fontFamilyName);
            if (j.contains("textColor"))
                j.at("textColor").get_to(a.textColor);
            if (j.contains("stroke"))
                j.at("stroke").get_to(a.stroke);
        }
    };
    void from_json(const json& j, ArticleContent& a) {
        ArticleContent::from_json(j, a);
    }

    class LifeTime {
    public:
        int begin_time = 0;
        int end_time = 0;
        static void from_json(const json& j, LifeTime& l) {
            j.at("begin_time").get_to(l.begin_time);
            if (j.contains("end_time"))
                j.at("end_time").get_to(l.end_time);
        }
    };
    void from_json(const json& j, LifeTime& l) {
        LifeTime::from_json(j, l);
    }
    
    class Track {
    public:
        std::string type;
        LifeTime lifetime;
        int zorder = 0;
        static void from_json(const json& j, Track& t) {
            j.at("type").get_to(t.type);
            j.at("lifetime").get_to(t.lifetime);
            if (j.contains("zorder"))
                j.at("zorder").get_to(t.zorder);
        }
    };
    void from_json(const json& j, Track& t) {
        Track::from_json(j, t);
    }

    class VideoTrack : public Track {
    public:
        VideoContent content;
        static void from_json(const json& j, VideoTrack& v) {
            Track::from_json(j, v);
            j.at("content").get_to(v.content);
        }
    };
    void from_json(const json& j, VideoTrack& v) {
        VideoTrack::from_json(j, v);
    }

    class GifTrack : public Track {
    public:
        VideoContent content;
        static void from_json(const json& j, GifTrack& g) {
            Track::from_json(j, g);
            j.at("content").get_to(g.content);
        }
    };
    void from_json(const json& j, GifTrack& g) {
        GifTrack::from_json(j, g);
    }

    class MusicTrack : public Track {
    public:
        AudioContent content;
        static void from_json(const json& j, MusicTrack& m) {
            Track::from_json(j, m);
            j.at("content").get_to(m.content);
        }
    };
    void from_json(const json& j, MusicTrack& m) {
        MusicTrack::from_json(j, m);
    }

    class VoiceTrack : public Track {
    public:
        AudioContent content;
        static void from_json(const json& j, VoiceTrack& v) {
            Track::from_json(j, v);
            j.at("content").get_to(v.content);
        }
    };
    void from_json(const json& j, VoiceTrack& v) {
        VoiceTrack::from_json(j, v);
    }

    class ImageTrack : public Track {
    public:
        ImageContent content;
        static void from_json(const json& j, ImageTrack& i) {
            Track::from_json(j, i);
            j.at("content").get_to(i.content);
        }
    };
    void from_json(const json& j, ImageTrack& i) {
        ImageTrack::from_json(j, i);
    }

    class TitleTrack : public Track {
    public:
        TitileContent content;
        static void from_json(const json& j, TitleTrack& t) {
            Track::from_json(j, t);
            j.at("content").get_to(t.content);
        }
    };
    void from_json(const json& j, TitleTrack& t) {
        TitleTrack::from_json(j, t);
    }

    class SubtitleTrack : public Track {
    public:
        TitileContent content;
        static void from_json(const json& j, SubtitleTrack& s) {
            Track::from_json(j, s);
            j.at("content").get_to(s.content);
        }
    };
    void from_json(const json& j, SubtitleTrack& s) {
        SubtitleTrack::from_json(j, s);
    }

    class ArticleTrack : public Track {
    public:
        ArticleContent content;
        static void from_json(const json& j, ArticleTrack& a) {
            Track::from_json(j, a);
            j.at("content").get_to(a.content);
        }
    };
    void from_json(const json& j, ArticleTrack& a) {
        ArticleTrack::from_json(j, a);
    }

    ///////////////////////////////////////
    class Story {
    public:
        int duration = 0;
        std::vector<Track*> tracks;
        static void from_json(const json& j, Story& s) {
            //j.at("tracks").get_to(s.tracks);
            for (auto& jtrack : j.at("tracks")) {
                if (jtrack.at("type").get<std::string>() == "video") {
                    auto t = new VideoTrack();
                    VideoTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "gif") {
                    auto t = new GifTrack();
                    GifTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "music") {
                    auto t = new MusicTrack();
                    MusicTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "voice") {
                    auto t = new VoiceTrack();
                    VoiceTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "image") {
                    auto t = new ImageTrack();
                    ImageTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "title") {
                    auto t = new TitleTrack();
                    TitleTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "subtitle") {
                    auto t = new SubtitleTrack();
                    SubtitleTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else if (jtrack.at("type").get<std::string>() == "article") {
                    auto t = new ArticleTrack();
                    ArticleTrack::from_json(jtrack, *t);
                    s.tracks.push_back(t);
                } else {

                }
            }
            
            if (j.contains("duration"))
                j.at("duration").get_to(s.duration);
        }
    };
    void from_json(const json& j, Story& s) {
        Story::from_json(j, s);
    }

    class MovieSpec {
    public:
        int width = 0;
        int height = 0;
        int fps = 0;
        int fileSizeLimit = 0;
        std::vector<Story> stories;

        static void from_json(const json& j, MovieSpec& m) {
            j.at("width").get_to(m.width);
            j.at("height").get_to(m.height);
            j.at("fps").get_to(m.fps);
            if (j.contains("fileSizeLimit") && j.at("fileSizeLimit").is_number())
                j.at("fileSizeLimit").get_to(m.fileSizeLimit);
            j.at("stories").get_to(m.stories);
        }
    };
    void from_json(const json& j, MovieSpec& m) {
        MovieSpec::from_json(j, m);
    }

    class Output {
    public:
        std::string url;
        static void from_json(const json& j, Output& o) {
            j.at("url").get_to(o.url);
        }
    };
    void from_json(const json& j, Output& o) {
        Output::from_json(j, o);
    }

    class Movie {
    public:
        std::string type = "movie";
        MovieSpec video;
        static void from_json(const json& j, Movie& m) {
            j.at("type").get_to(m.type);
            j.at("video").get_to(m.video);
        }
    };
    void from_json(const json& j, Movie& m) {
        Movie::from_json(j, m);
    }

}
