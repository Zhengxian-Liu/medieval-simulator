// The villagers' decision model, shared by the terminal (play) and browser (webplay) front ends. The model is
// run as a separate command per question, several at once (JSON on stdin, "index<TAB>reason" on stdout). Every
// answer is logged in order; a log given at start is replayed first, so a game replays without calling the model.
#pragma once

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "simcore/world.h"

namespace sim {

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

inline int parseIndex(const std::string& s) {
    try {
        size_t used = 0;
        int n = std::stoi(s, &used);
        return used == s.size() ? n : -1;
    } catch (...) {
        return -1;
    }
}

class NpcBridge {
public:
    // model: the command to run ("" = rules only); logPath: answers to replay and append to ("" = none).
    void attach(World& w, const std::string& model, const std::string& logPath, const std::string& scope) {
        w_ = &w;
        model_ = model;
        logPath_ = logPath;
        if (!logPath_.empty()) {
            std::ifstream in(logPath_);
            std::string line;
            while (std::getline(in, line)) {
                size_t tab = line.find('\t');
                replay_.push_back({parseIndex(line.substr(0, tab)), tab == std::string::npos ? "" : line.substr(tab + 1)});
            }
            log_.open(logPath_, std::ios::app);
        }
        w.setNpcScope(scope == "all" ? World::kFar : scope == "near" ? World::kNear : World::kSpotlight);
        if (model_.empty() && logPath_.empty()) return;
        w.setNpcDecider([this](const World::NpcChoice& c, std::string& reason) -> int {
            auto [pick, why] = decideAll({c})[0];
            reason = why;
            return pick;
        });
        w.setNpcBatchDecider([this](const std::vector<World::NpcChoice>& cs) { return decideAll(cs); });
    }

private:
    World* w_ = nullptr;
    std::string model_, logPath_;
    std::vector<std::pair<int, std::string>> replay_;
    size_t pos_ = 0;
    std::ofstream log_;

    std::string requestOf(const World::NpcChoice& c) const {
        std::string req = std::string("{\"who\": \"") + jsonEscape(w_->fullName(c.who)) + "\", \"title\": \"" + jsonEscape(c.title) +
                          "\", \"situation\": \"" + jsonEscape(c.situation) + "\", \"mind\": \"" + jsonEscape(c.mind) +
                          "\", \"feelings\": \"" + jsonEscape(c.feelings) + "\", \"options\": [";
        for (size_t i = 0; i < c.options.size(); ++i) req += (i ? ", " : "") + std::string("\"") + jsonEscape(c.options[i]) + "\"";
        req += "], \"rule_weights\": [";
        for (size_t i = 0; i < c.weights.size(); ++i) req += (i ? ", " : "") + std::to_string(c.weights[i]);
        req += "], \"fallback\": " + std::to_string(c.fallback) + "}";
        return req;
    }

    std::vector<std::pair<int, std::string>> askModel(const std::vector<World::NpcChoice>& cs) {
        std::vector<std::pair<int, std::string>> out(cs.size(), {-1, ""});
        const size_t kAtOnce = 16;
        std::string base = logPath_.empty() ? std::string("npc") : logPath_;
        for (size_t from = 0; from < cs.size(); from += kAtOnce) {
            size_t to = std::min(cs.size(), from + kAtOnce);
            std::vector<std::string> paths;
            std::vector<FILE*> running;
            for (size_t i = from; i < to; ++i) {
                paths.push_back(base + "." + std::to_string(i - from) + ".request.json");
                {
                    std::ofstream rq(paths.back());
                    rq << requestOf(cs[i]);
                }
                running.push_back(popen((model_ + " < '" + paths.back() + "'").c_str(), "r"));
            }
            for (size_t i = from; i < to; ++i) {
                if (FILE* f = running[i - from]) {
                    char buf[4096];
                    std::string text;
                    while (fgets(buf, sizeof buf, f)) text += buf;
                    pclose(f);
                    std::string first = text.substr(0, text.find('\n'));
                    size_t tab = first.find('\t');
                    out[i] = {parseIndex(first.substr(0, tab)), tab == std::string::npos ? "" : first.substr(tab + 1)};
                }
                std::remove(paths[i - from].c_str());
            }
        }
        return out;
    }

    std::vector<std::pair<int, std::string>> decideAll(const std::vector<World::NpcChoice>& cs) {
        std::vector<std::pair<int, std::string>> out;
        std::vector<World::NpcChoice> fresh;
        for (const auto& c : cs) {
            if (pos_ < replay_.size()) out.push_back(replay_[pos_++]);
            else fresh.push_back(c);
        }
        if (!fresh.empty()) {
            auto got = model_.empty() ? std::vector<std::pair<int, std::string>>(fresh.size(), {-1, ""}) : askModel(fresh);
            for (size_t i = 0; i < fresh.size(); ++i) {
                auto [pick, reason] = got[i];
                if (pick < 0 || pick >= static_cast<int>(fresh[i].options.size())) {
                    pick = -1;
                    reason.clear();
                }
                if (log_.is_open()) log_ << pick << "\t" << reason << "\n" << std::flush;
                out.emplace_back(pick, reason);
            }
        }
        return out;
    }
};

}  // namespace sim
