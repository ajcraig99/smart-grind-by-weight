// grindsim - native command-line runner for the digital twin.
//
//   grindsim --seed 7 --target 18.0 --params p.json --scenario s.json --out run.csv
//   grindsim --batch 200 --seed 1000 --jobs 4 --scenario s.json --summary summary.csv
//
// Each run is a fresh process image (batch mode forks one child per run) because the
// firmware's global objects are constructed once per process, as on the device.
#include "../core/runtime.h"
#include "../core/scheduler.h"
#include "../core/world.h"
#include "../sim_api.h"
#include "png_writer.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
    uint64_t seed = 1;
    double target = -1;
    std::string params_path;
    std::string scenario_path;
    std::string scenario_json;
    std::string out_path;
    std::string summary_path;
    std::string log_path;
    std::vector<std::pair<std::string, double>> param_overrides;
    double max_s = 400;
    uint32_t trace_ms = 10;
    int batch = 0;
    int jobs = 1;
    bool echo_log = false;
    std::string resume_path;
    int summary_fd = -1;
    std::string screens_dir;
    uint32_t screen_every_ms = 1000;
    double post_reset_observe_s = 20.0;
};

std::vector<std::string> g_argv;

void usage() {
    std::fprintf(stderr,
                 "usage: grindsim [--seed N] [--target G] [--params p.json] [--scenario s.json]\n"
                 "                [--set name=value]... [--out run.csv] [--summary summary.csv]\n"
                 "                [--log log.txt] [--trace-ms 10] [--max-s 400] [--echo-log]\n"
                 "                [--batch N --jobs J] [--screens DIR --screen-every-ms 1000]\n");
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--seed") o.seed = std::strtoull(next("--seed"), nullptr, 10);
        else if (a == "--target") o.target = std::atof(next("--target"));
        else if (a == "--params") o.params_path = next("--params");
        else if (a == "--scenario") o.scenario_path = next("--scenario");
        else if (a == "--out") o.out_path = next("--out");
        else if (a == "--summary") o.summary_path = next("--summary");
        else if (a == "--log") o.log_path = next("--log");
        else if (a == "--trace-ms") o.trace_ms = static_cast<uint32_t>(std::atoi(next("--trace-ms")));
        else if (a == "--max-s") o.max_s = std::atof(next("--max-s"));
        else if (a == "--batch") o.batch = std::atoi(next("--batch"));
        else if (a == "--jobs") o.jobs = std::atoi(next("--jobs"));
        else if (a == "--echo-log") o.echo_log = true;
        else if (a == "--resume") o.resume_path = next("--resume");
        else if (a == "--summary-fd") o.summary_fd = std::atoi(next("--summary-fd"));
        else if (a == "--screens") o.screens_dir = next("--screens");
        else if (a == "--screen-every-ms") o.screen_every_ms = static_cast<uint32_t>(std::atoi(next("--screen-every-ms")));
        else if (a == "--post-reset-s") o.post_reset_observe_s = std::atof(next("--post-reset-s"));
        else if (a == "--set") {
            const std::string kv = next("--set");
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "--set expects name=value\n"); return false; }
            o.param_overrides.emplace_back(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1));
        } else if (a == "-h" || a == "--help") {
            usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown argument %s\n", a.c_str());
            usage();
            return false;
        }
    }
    if (!o.scenario_path.empty() && !read_file(o.scenario_path, o.scenario_json)) {
        std::fprintf(stderr, "cannot read scenario %s\n", o.scenario_path.c_str());
        return false;
    }
    return true;
}

const char* result_name(int r) {
    static const char* names[] = {"UNKNOWN", "SUCCESS", "OVERSHOOT", "MAX_PULSES", "TIMEOUT", "ERROR", "SCALE_ERROR"};
    return r >= 0 && r < 7 ? names[r] : "UNKNOWN";
}

std::string csv_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else if (c != '\n' && c != '\r') out += c;
    }
    return out + "\"";
}

const char* kSummaryHeader =
    "seed,scenario,grind,target_g,loaded_g,terminal,result,error,fw_final_g,true_cup_end_g,true_cup_g,"
    "err_true_g,err_fw_g,fw_minus_true_g,pulses,purge_prompts,tares,grind_time_s,motor_on_s,motor_max_run_s,"
    "motor_invalid_signal_s,motor_no_sample_s,motor_after_end_s,motor_no_cup_s,latency_ms,stop_offset_g,"
    "burr_left_g,chute_g,platform_g,spilled_g,operator_status\n";

std::string summary_rows(const Options& o, const std::string& scenario_name) {
    std::string rows;
    const auto& records = sim::runtime_records();
    char line[1024];
    for (const auto& r : records) {
        const bool ended = r.terminal_phase >= 0;
        const double true_final = r.open ? 0 : r.true_cup_closed_g;
        const double err_true = true_final - r.target_g;
        const double err_fw = r.fw_final_seen ? r.fw_final_g - r.target_g : NAN;
        std::snprintf(line, sizeof(line),
                      "%llu,%s,%u,%.3f,%.3f,%s,%s,%s,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.4f,%.3f,%.3f,%.3f,%.3f,%s\n",
                      static_cast<unsigned long long>(o.seed), csv_escape(scenario_name).c_str(), r.index,
                      r.target_g, r.loaded_g, ended ? sim::firmware_phase_name(r.terminal_phase) : "NONE",
                      ended ? result_name(r.result) : "NONE", csv_escape(r.error).c_str(), r.fw_final_g,
                      r.true_cup_end_g, true_final, err_true, err_fw, r.fw_final_seen ? r.fw_final_g - true_final : NAN,
                      r.pulses, r.purge_prompts, r.tares, ended ? r.t_end_s - r.t_start_s : -1.0, r.motor_on_s,
                      r.motor_on_max_run_s, r.motor_invalid_signal_s, r.motor_no_sample_s, r.motor_after_end_s,
                      r.motor_no_cup_s, r.latency_ms, r.stop_offset_g, r.burr_left_g, r.chute_g,
                      r.true_platform_closed_g, r.spilled_g, csv_escape(sim::operator_status()).c_str());
        rows += line;
    }
    if (records.empty()) {
        std::snprintf(line, sizeof(line), "%llu,%s,-1,%.3f,0,NONE,NONE,\"\",NAN,0,0,NAN,NAN,NAN,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,%s\n",
                      static_cast<unsigned long long>(o.seed), csv_escape(scenario_name).c_str(), o.target,
                      csv_escape(sim::operator_status()).c_str());
        rows += line;
    }
    return rows;
}

std::string scenario_name_of(const std::string& json) {
    const size_t p = json.find("\"name\"");
    if (p == std::string::npos) return "normal";
    const size_t q1 = json.find('"', json.find(':', p) + 1);
    const size_t q2 = json.find('"', q1 + 1);
    return q1 == std::string::npos || q2 == std::string::npos ? "normal" : json.substr(q1 + 1, q2 - q1 - 1);
}

void write_all(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t w = write(fd, data.data() + off, data.size() - off);
        if (w <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(w);
    }
}

// Re-execute this program to continue after a firmware reset: a new process image gives the
// firmware fresh global objects (RAM lost), while NVS, LittleFS and the plant are carried in `blob`.
[[noreturn]] void exec_resume(const Options& o, const std::string& blob, int summary_fd) {
    std::vector<std::string> args;
    for (size_t i = 0; i < g_argv.size(); ++i) {
        const std::string& a = g_argv[i];
        if (a == "--resume" || a == "--summary-fd" || a == "--batch" || a == "--jobs" || a == "--seed" ||
            a == "--summary") {
            ++i;  // drop the flag and its value
            continue;
        }
        args.push_back(a);
    }
    args.push_back("--seed");
    args.push_back(std::to_string(static_cast<unsigned long long>(o.seed)));
    args.push_back("--resume");
    args.push_back(blob);
    args.push_back("--summary-fd");
    args.push_back(std::to_string(summary_fd));
    if (!o.out_path.empty()) {
        // keep --out: the resumed run appends to it
    }
    std::vector<char*> cargv;
    for (auto& a : args) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);
    execv("/proc/self/exe", cargv.data());
    std::perror("grindsim: execv");
    _exit(3);
}

struct RunResult {
    std::string rows;
    bool restart = false;
    std::string blob_path;
};

// One run in this process: a fresh boot, or the continuation after a reset (--resume).
RunResult run_once(const Options& o) {
    RunResult result;
    sim_create(o.seed);
    if (o.echo_log) sim::log_set_echo(true);
    if (!o.params_path.empty()) {
        std::string text;
        if (!read_file(o.params_path, text) || sim_load_params_json(text.c_str()) != 0) {
            std::fprintf(stderr, "grindsim: problem loading params %s\n", o.params_path.c_str());
            std::exit(2);
        }
    }
    for (const auto& kv : o.param_overrides) {
        if (sim_set_param(kv.first.c_str(), kv.second) != 0) {
            std::fprintf(stderr, "grindsim: unknown or invalid parameter %s\n", kv.first.c_str());
            std::exit(2);
        }
    }
    std::string scenario = o.scenario_json.empty() ? "{\"name\":\"normal\"}" : o.scenario_json;
    std::string error;
    if (!sim::operator_load_scenario_json(scenario, &error)) {
        std::fprintf(stderr, "grindsim: bad scenario: %s\n", error.c_str());
        std::exit(2);
    }
    if (o.target > 0) sim::operator_set_target(static_cast<float>(o.target));
    const bool resuming = !o.resume_path.empty();
    if (resuming) {
        std::string blob;
        if (!read_file(o.resume_path, blob) ||
            sim_persist_import(reinterpret_cast<const uint8_t*>(blob.data()), blob.size()) != 0) {
            std::fprintf(stderr, "grindsim: cannot resume from %s\n", o.resume_path.c_str());
            std::exit(2);
        }
        std::remove(o.resume_path.c_str());
    } else {
        sim::operator_seed_preferences();
    }
    sim_trace_period_ms(o.out_path.empty() ? 0 : o.trace_ms);
    if (resuming) sim::trace_suppress_header();

    FILE* out = o.out_path.empty() ? nullptr : std::fopen(o.out_path.c_str(), resuming ? "ab" : "wb");
    FILE* log = o.log_path.empty() ? nullptr : std::fopen(o.log_path.c_str(), resuming ? "ab" : "wb");
    std::vector<char> buf(1 << 20);
    if (resuming && log) std::fprintf(log, "\n[SIM] ===== firmware reset: new boot at %.3f s =====\n", sim::now_us() / 1e6);

    sim_boot();
    if (resuming) sim::operator_start_post_reset(o.post_reset_observe_s);
    else sim::operator_start();
    const uint64_t end_us = static_cast<uint64_t>(o.max_s * 1e6);
    while (sim::now_us() < end_us) {
        sim_run_ms(100);
        size_t n;
        while ((n = sim_trace_read(buf.data(), buf.size())) > 0) if (out) std::fwrite(buf.data(), 1, n, out);
        while ((n = sim_log_read(buf.data(), buf.size())) > 0) if (log) std::fwrite(buf.data(), 1, n, log);
        if (!o.screens_dir.empty() && o.screen_every_ms > 0 &&
            (sim::now_us() / 1000ULL) % o.screen_every_ms < 100 && sim_framebuffer_take_dirty()) {
            char name[64];
            std::snprintf(name, sizeof(name), "/screen_%07llu.png",
                          static_cast<unsigned long long>(sim::now_us() / 1000ULL));
            sim::write_png_rgb565(o.screens_dir + name, sim_framebuffer(), sim::kScreenWidth, sim::kScreenHeight,
                                  sim_display_on() ? 1.0f : 0.0f);
        }
        if (sim::world_restart_requested()) {
            result.restart = true;
            break;
        }
        if (sim::operator_finished()) break;
    }
    if (result.restart) {
        const size_t len = sim_persist_export(nullptr, 0);
        std::vector<uint8_t> blob(len);
        sim_persist_export(blob.data(), blob.size());
        char path[] = "/tmp/grindsim-reset-XXXXXX";
        const int fd = mkstemp(path);
        if (fd >= 0) {
            write_all(fd, std::string(reinterpret_cast<const char*>(blob.data()), blob.size()));
            close(fd);
            result.blob_path = path;
        }
    } else {
        sim::runtime_close_records();
    }
    if (out) std::fclose(out);
    if (log) std::fclose(log);
    result.rows = summary_rows(o, scenario_name_of(scenario));
    return result;
}

int run_batch(const Options& base) {
    FILE* summary = base.summary_path.empty() ? stdout : std::fopen(base.summary_path.c_str(), "wb");
    if (!summary) {
        std::fprintf(stderr, "cannot write %s\n", base.summary_path.c_str());
        return 2;
    }
    std::fputs(kSummaryHeader, summary);
    const int jobs = base.jobs < 1 ? 1 : base.jobs;
    std::map<int, std::string> results;
    std::map<pid_t, std::pair<int, int>> running;  // pid -> (index, read fd)
    int next = 0, written = 0;
    auto collect_one = [&]() {
        int status = 0;
        const pid_t pid = wait(&status);
        if (pid <= 0) return;
        auto it = running.find(pid);
        if (it == running.end()) return;
        std::string rows;
        char chunk[4096];
        ssize_t n;
        while ((n = read(it->second.second, chunk, sizeof(chunk))) > 0) rows.append(chunk, static_cast<size_t>(n));
        close(it->second.second);
        if (rows.empty()) {
            char line[256];
            std::snprintf(line, sizeof(line), "%llu,CRASH,-1,%.3f,0,NONE,CRASH,\"child status %d\",NAN,0,0,NAN,NAN,NAN,0,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,0,\"crash\"\n",
                          static_cast<unsigned long long>(base.seed + static_cast<uint64_t>(it->second.first)), base.target, status);
            rows = line;
        }
        results[it->second.first] = rows;
        running.erase(it);
    };
    while (next < base.batch || !running.empty()) {
        while (next < base.batch && static_cast<int>(running.size()) < jobs) {
            int fds[2];
            if (pipe(fds) != 0) return 3;
            const pid_t pid = fork();
            if (pid == 0) {
                close(fds[0]);
                Options o = base;
                o.seed = base.seed + static_cast<uint64_t>(next);
                o.out_path.clear();
                o.log_path.clear();
                const RunResult r = run_once(o);
                write_all(fds[1], r.rows);
                if (r.restart && !r.blob_path.empty()) exec_resume(o, r.blob_path, fds[1]);
                close(fds[1]);
                _exit(0);
            }
            close(fds[1]);
            running[pid] = {next, fds[0]};
            ++next;
        }
        // Children write at most a few KB, well under the pipe buffer, so waiting first is safe.
        collect_one();
        while (results.count(written)) {
            std::fputs(results[written].c_str(), summary);
            results.erase(written);
            ++written;
        }
        std::fflush(summary);
    }
    if (summary != stdout) std::fclose(summary);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 0; i < argc; ++i) g_argv.emplace_back(argv[i]);
    Options o;
    if (!parse_args(argc, argv, o)) return 2;
    if (o.batch > 0) return run_batch(o);
    int fd = o.summary_fd;
    if (fd < 0) {
        if (!o.summary_path.empty()) {
            fd = open(o.summary_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                std::fprintf(stderr, "cannot write %s\n", o.summary_path.c_str());
                return 2;
            }
        } else {
            fd = 1;
        }
        write_all(fd, kSummaryHeader);
    }
    const RunResult r = run_once(o);
    write_all(fd, r.rows);
    if (r.restart && !r.blob_path.empty()) exec_resume(o, r.blob_path, fd);
    if (fd > 2) close(fd);
    return 0;
}
