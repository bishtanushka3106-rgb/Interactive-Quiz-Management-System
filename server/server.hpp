#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "httplib.h"
#include "json.hpp"
#include "question_bank.h"
#include "quiz.h"

using json = nlohmann::json;
using namespace std;

static mutex gMutex;
static mt19937_64 gRng{random_device{}()};
static string gDataFile = "data/questions.json";
static string gAdminPassword = "admin123";
static const int QUIZ_LENGTH = 10;
static const size_t MAX_SESSIONS = 500;

struct Session {
    vector<Question> questions;
    vector<int> state;
    vector<string> given;
    int score = 0;
    int answered = 0;
    time_t created = 0;
};

static map<string, Session> gSessions;
static set<string> gAdminTokens;

static string randomId() {
    static const char* hex = "0123456789abcdef";
    string id;
    for (int i = 0; i < 24; i++) id += hex[gRng() % 16];
    return id;
}

static string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

static string lower(string s) {
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c) { return (char)tolower(c); });
    return s;
}

static void sendJson(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body.dump(), "application/json");
}

static void sendError(httplib::Response& res, int status, const string& msg) {
    sendJson(res, status, json{{"error", msg}});
}

static bool parseBody(const httplib::Request& req, httplib::Response& res,
                      json& out, bool allowEmpty = false) {
    if (trim(req.body).empty()) {
        if (allowEmpty) {
            out = json::object();
            return true;
        }
        sendError(res, 400, "Request body is empty.");
        return false;
    }
    out = json::parse(req.body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        sendError(res, 400, "Request body must be a JSON object.");
        return false;
    }
    return true;
}

static bool isAdmin(const httplib::Request& req) {
    string h = req.get_header_value("Authorization");
    const string prefix = "Bearer ";
    if (h.compare(0, prefix.size(), prefix) != 0) return false;
    return gAdminTokens.count(h.substr(prefix.size())) > 0;
}

static json questionToJson(const Question& q) {
    json j = {{"topic", q.topic},
              {"difficulty", q.difficulty},
              {"text", q.text},
              {"isCode", q.isCode},
              {"explanation", q.explanation}};
    if (q.isCode) {
        j["codeAnswer"] = q.codeAnswer;
    } else {
        json opts = json::array();
        for (const Option& o : q.options)
            opts.push_back({{"text", o.text}, {"correct", o.correct}});
        j["options"] = opts;
    }
    return j;
}

static Question questionFromJson(const json& j) {
    Question q;
    q.topic = j.value("topic", "");
    q.difficulty = j.value("difficulty", "");
    q.text = j.value("text", "");
    q.explanation = j.value("explanation", "");
    q.isCode = j.value("isCode", false);
    q.codeAnswer = j.value("codeAnswer", "");
    if (j.contains("options") && j["options"].is_array()) {
        for (const json& o : j["options"])
            q.options.push_back({o.value("text", ""), o.value("correct", false)});
    }
    return q;
}


static void saveQuestions() {
        json arr = json::array();
    for (const Question& q : getQuestionBank().getAllQuestions())
        arr.push_back(questionToJson(q));

    filesystem::path path(gDataFile);
    if (path.has_parent_path())
        filesystem::create_directories(path.parent_path());

    string tmp = gDataFile + ".tmp";
    {
        ofstream out(tmp);
        out << arr.dump(2);
    }
    filesystem::rename(tmp, gDataFile);
}

static void loadOrSeedQuestions() {
    QuestionBank& bank = getQuestionBank();

    ifstream in(gDataFile);
    if (in) {
        json arr = json::parse(in, nullptr, false);
        if (arr.is_array()) {
            for (const json& j : arr) bank.addQuestion(questionFromJson(j));
            cout << "Loaded " << bank.size() << " questions from "
                 << gDataFile << "\n";
            return;
        }
        cerr << "Could not read " << gDataFile
             << "; starting from the built-in questions.\n";
    }

    loadQuestions();
    saveQuestions();
    cout << "Seeded " << bank.size() << " built-in questions.\n";
}
static string validateNewQuestion(const json& j, Question& out) {
    string topic = trim(j.value("topic", ""));
    string difficulty = trim(j.value("difficulty", ""));
    string text = trim(j.value("text", ""));
    string explanation = trim(j.value("explanation", ""));
    bool isCode = j.value("isCode", false);

    if (topic.empty() || topic.size() > 40)
        return "Topic is required (up to 40 characters).";
    if (difficulty != "Easy" && difficulty != "Medium" && difficulty != "Hard")
        return "Difficulty must be Easy, Medium or Hard.";
    if (text.empty() || text.size() > 1000)
        return "Question text is required (up to 1000 characters).";
    if (explanation.size() > 1000)
        return "Explanation must be 1000 characters or fewer.";

    out = Question();
    out.topic = topic;
    out.difficulty = difficulty;
    out.text = text;
    out.explanation = explanation;
    out.isCode = isCode;

    if (isCode) {
        string answer = trim(j.value("codeAnswer", ""));
        if (answer.empty() || answer.size() > 200)
            return "Expected output is required (up to 200 characters).";
        out.codeAnswer = answer;
        return "";
    }

    if (!j.contains("options") || !j["options"].is_array())
        return "Provide the answer options.";
    vector<string> opts;
    for (const json& o : j["options"]) {
        string s = o.is_string() ? trim(o.get<string>()) : "";
        if (!s.empty()) opts.push_back(s);
    }
    if (opts.size() < 2 || opts.size() > 6)
        return "Provide between 2 and 6 non-empty options.";

    int correct = j.value("correctIndex", -1);
    vector<string> submitted;
    for (const json& o : j["options"])
        submitted.push_back(o.is_string() ? trim(o.get<string>()) : "");
    if (correct < 0 || correct >= (int)submitted.size() || submitted[correct].empty())
        return "Choose which option is correct.";

    for (int i = 0; i < (int)submitted.size(); i++) {
        if (submitted[i].empty()) continue;
        out.options.push_back({submitted[i], i == correct});
    }
    return "";
}

static string envOr(const char* name, const string& fallback) {
    const char* v = getenv(name);
    return (v && *v) ? string(v) : fallback;
}

int main() {
    string host = envOr("HOST", "127.0.0.1");
    int port = atoi(envOr("PORT", "8080").c_str());
    string frontendDir = envOr("FRONTEND_DIR", "../frontend");
    gDataFile = envOr("DATA_FILE", gDataFile);
    gAdminPassword = envOr("ADMIN_PASSWORD", gAdminPassword);

    loadOrSeedQuestions();

    httplib::Server svr;

    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                 std::exception_ptr) {
        sendError(res, 500, "Something went wrong on the server.");
    });
    svr.Get("/api/topics", [](const httplib::Request&, httplib::Response& res) {
        lock_guard<mutex> lock(gMutex);
        map<string, int> counts;
        vector<Question> all = getQuestionBank().getAllQuestions();
        for (const Question& q : all) counts[q.topic]++;

        json topics = json::array();
        for (auto& kv : counts)
            topics.push_back({{"name", kv.first}, {"count", kv.second}});
        sendJson(res, 200, {{"total", (int)all.size()}, {"topics", topics}});
    });

    svr.Post("/api/quiz/start", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseBody(req, res, body, true)) return;
        string topic = body.value("topic", "");

        lock_guard<mutex> lock(gMutex);

        vector<Question> pool;
        for (const Question& q : getQuestionBank().getAllQuestions())
            if (topic.empty() || lower(q.topic) == lower(topic)) pool.push_back(q);

        if (pool.empty()) {
            sendError(res, 404, topic.empty()
                                    ? "The question bank is empty. Ask an admin to add questions."
                                    : "No questions found for topic: " + topic);
            return;
        }

        shuffle(pool.begin(), pool.end(), gRng);
        size_t n = min((size_t)QUIZ_LENGTH, pool.size());
        pool.resize(n);

        Session s;
        s.created = time(nullptr);
        s.state.assign(n, -1);
        s.given.assign(n, "");
        for (Question& q : pool) {
            if (!q.isCode) shuffle(q.options.begin(), q.options.end(), gRng);
            s.questions.push_back(q);
        }

        if (gSessions.size() >= MAX_SESSIONS) {
            auto oldest = gSessions.begin();
            for (auto it = gSessions.begin(); it != gSessions.end(); ++it)
                if (it->second.created < oldest->second.created) oldest = it;
            gSessions.erase(oldest);
        }

        string id = randomId();
        json questions = json::array();
        for (const Question& q : s.questions) {
            json j = {{"topic", q.topic},
                      {"difficulty", q.difficulty},
                      {"text", q.text},
                      {"isCode", q.isCode}};
            if (!q.isCode) {
                json opts = json::array();
                for (const Option& o : q.options) opts.push_back(o.text);
                j["options"] = opts;
            }
            questions.push_back(j);
        }
        gSessions[id] = move(s);
        sendJson(res, 200, {{"sessionId", id}, {"total", (int)n}, {"questions", questions}});
    });

   svr.Post("/api/quiz/answer", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseBody(req, res, body)) return;

        lock_guard<mutex> lock(gMutex);

        auto it = gSessions.find(body.value("sessionId", ""));
        if (it == gSessions.end()) {
            sendError(res, 404, "This quiz session has expired. Start a new quiz.");
            return;
        }
        Session& s = it->second;

        int index = body.value("index", -1);
        if (index < 0 || index >= (int)s.questions.size()) {
            sendError(res, 400, "Unknown question number.");
            return;
        }
        if (s.state[index] != -1) {
            sendError(res, 409, "You have already answered this question.");
            return;
        }

        const Question& q = s.questions[index];
        bool correct = false;
        string given, correctAnswer;

        if (q.isCode) {
            given = trim(body.value("text", ""));
            correctAnswer = q.codeAnswer;
            correct = (given == trim(q.codeAnswer));
        } else {
            int choice = body.value("choice", -1);
            if (choice < 0 || choice >= (int)q.options.size()) {
                sendError(res, 400, "Choose one of the options.");
                return;
            }
            given = q.options[choice].text;
            correct = q.options[choice].correct;
            for (const Option& o : q.options)
                if (o.correct) correctAnswer = o.text;
        }

        s.state[index] = correct ? 1 : 0;
        s.given[index] = given;
        s.answered++;
        if (correct) s.score++;

        bool finished = (s.answered == (int)s.questions.size());
        json out = {{"correct", correct},
                    {"correctAnswer", correctAnswer},
                    {"explanation", q.explanation},
                    {"score", s.score},
                    {"answered", s.answered},
                    {"total", (int)s.questions.size()},
                    {"finished", finished}};

        if (finished) {
            json review = json::array();
            for (size_t i = 0; i < s.questions.size(); i++) {
                const Question& rq = s.questions[i];
                string ans = rq.codeAnswer;
                if (!rq.isCode)
                    for (const Option& o : rq.options)
                        if (o.correct) ans = o.text;
                review.push_back({{"topic", rq.topic},
                                  {"difficulty", rq.difficulty},
                                  {"text", rq.text},
                                  {"correct", s.state[i] == 1},
                                  {"yourAnswer", s.given[i]},
                                  {"correctAnswer", ans}});
            }
            out["review"] = review;
            gSessions.erase(it);
        }
        sendJson(res, 200, out);
    });

    svr.Post("/api/admin/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseBody(req, res, body)) return;

        if (body.value("password", "") != gAdminPassword) {
            sendError(res, 401, "Wrong password.");
            return;
        }
        lock_guard<mutex> lock(gMutex);
        string token = randomId() + randomId();
        gAdminTokens.insert(token);
        sendJson(res, 200, {{"token", token}});
    });

    svr.Post("/api/admin/logout", [](const httplib::Request& req, httplib::Response& res) {
        lock_guard<mutex> lock(gMutex);
        string h = req.get_header_value("Authorization");
        if (h.rfind("Bearer ", 0) == 0) gAdminTokens.erase(h.substr(7));
        sendJson(res, 200, {{"ok", true}});
    });

    svr.Get("/api/admin/questions", [](const httplib::Request& req, httplib::Response& res) {
        lock_guard<mutex> lock(gMutex);
        if (!isAdmin(req)) {
            sendError(res, 401, "Log in as admin first.");
            return;
        }
        string topic = trim(req.get_param_value("topic"));
        vector<Question> all = getQuestionBank().getAllQuestions();

        json arr = json::array();
        for (size_t i = 0; i < all.size(); i++) {
            if (!topic.empty() &&
                lower(all[i].topic).find(lower(topic)) == string::npos)
                continue;
            json j = questionToJson(all[i]);
            j["position"] = (int)i + 1;
            arr.push_back(j);
        }
        sendJson(res, 200, {{"total", (int)all.size()}, {"questions", arr}});
    });

    svr.Post("/api/admin/questions", [](const httplib::Request& req, httplib::Response& res) {
        {
            lock_guard<mutex> lock(gMutex);
            if (!isAdmin(req)) {
                sendError(res, 401, "Log in as admin first.");
                return;
            }
        }
        json body;
        if (!parseBody(req, res, body)) return;

        Question q;
        string problem = validateNewQuestion(body, q);
        if (!problem.empty()) {
            sendError(res, 400, problem);
            return;
        }

        lock_guard<mutex> lock(gMutex);
        getQuestionBank().addQuestion(q);
        saveQuestions();
        sendJson(res, 201, {{"position", getQuestionBank().size()},
                            {"total", getQuestionBank().size()}});
    });

    svr.Delete("/api/admin/questions/:position",
               [](const httplib::Request& req, httplib::Response& res) {
        lock_guard<mutex> lock(gMutex);
        if (!isAdmin(req)) {
            sendError(res, 401, "Log in as admin first.");
            return;
        }
        int position = 0;
        try {
            position = stoi(req.path_params.at("position"));
        } catch (...) {
            sendError(res, 400, "Position must be a number.");
            return;
        }
        if (!getQuestionBank().deleteQuestion(position)) {
            sendError(res, 404, "There is no question at that position.");
            return;
        }
        saveQuestions();
        sendJson(res, 200, {{"total", getQuestionBank().size()}});
    });

   if (!svr.set_mount_point("/", frontendDir)) {
        cerr << "Frontend folder not found: " << frontendDir
             << " (set FRONTEND_DIR)\n";
        return 1;
    }

    cout << "DSA Quiz running at http://" << host << ":" << port << "\n";
    cout << "Admin password: "
         << (getenv("ADMIN_PASSWORD") ? "(from ADMIN_PASSWORD)" : "admin123")
         << "\n";

    if (!svr.listen(host, port)) {
        cerr << "Could not listen on " << host << ":" << port
             << ". Is the port already in use? Set PORT to use another.\n";
        return 1;
    }
    return 0;
}
