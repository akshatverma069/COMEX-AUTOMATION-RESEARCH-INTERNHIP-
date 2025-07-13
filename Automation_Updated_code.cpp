#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include<fstream>
#include<bits/stdc++.h>


// Utility function to run a system command and check its return value
void runCmd(const std::string &cmd) {
    std::cout << "[CMD] " << cmd << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "Command failed with exit code " << ret << ": " << cmd << std::endl;
        // Optionally, exit or handle the error
        exit(ret);
    }
}

// Function to capture the output of a system command
std::string execCmd(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Function to run strace on a given package name
void run_strace(const std::string& package_name) {
    // Step 1: Get all PIDs of the package
    std::string pid_cmd = "adb shell ps | grep " + package_name + " | awk '{print $2}'";
    std::cout << "Fetching PIDs for package: " << package_name << std::endl;

    std::string pids_str = execCmd(pid_cmd);
    std::istringstream iss(pids_str);
    std::vector<std::string> pids;
    std::string pid;
    while (iss >> pid) {
        pids.push_back(pid);
    }

    if (pids.empty()) {
        std::cerr << "Error: No PIDs found for package " << package_name << std::endl;
        return;
    }

    // Step 2: Start strace for all PIDs in background, save their strace pids
    for (const auto& pid : pids) {
        std::string out_file = "/data/local/tmp/strace_" + pid + ".out";
        std::string pid_file = "/data/local/tmp/strace_" + pid + ".pid";

        std::string start_strace_cmd =
            "adb shell su -c \"nohup /data/local/tmp/strace -p " + pid +
            " -f -tt -T -o " + out_file + " -e trace=all </dev/null >/dev/null 2>&1 & echo $! > " + pid_file + "\"";

        std::cout << "[CMD] " << start_strace_cmd << std::endl;
        runCmd(start_strace_cmd);

        std::cout << "Started strace on PID " << pid << std::endl;
    }

    // Step 3: Sleep total 20 seconds
    std::cout << "Sleeping for 20 seconds while strace runs on all PIDs..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(20));

    // Step 4: Kill all strace processes
    for (const auto& pid : pids) {
        std::string pid_file = "/data/local/tmp/strace_" + pid + ".pid";
        std::string read_pid_cmd = "adb shell cat " + pid_file;
        std::string strace_pid = execCmd(read_pid_cmd);
        strace_pid.erase(strace_pid.find_last_not_of(" \n\r\t") + 1);

        if (!strace_pid.empty()) {
            std::string kill_cmd = "adb shell su -c \"kill " + strace_pid + "\"";
            std::cout << "[CMD] " << kill_cmd << std::endl;
            runCmd(kill_cmd);

            std::string rm_pid_file_cmd = "adb shell su -c \"rm " + pid_file + "\"";
            runCmd(rm_pid_file_cmd);

            std::cout << "Stopped strace process " << strace_pid << " for PID " << pid << std::endl;
        } else {
            std::cerr << "Could not read strace PID for process " << pid << std::endl;
        }
    }

    std::cout << "All strace processes stopped after 20 seconds." << std::endl;

    for (const auto& pid : pids) {
    std::string remote_out = "/data/local/tmp/strace_" + pid + ".out";
    std::string local_out = "./results/" + package_name + "_" + pid + ".strace";
    runCmd("adb pull " + remote_out + " " + local_out);
}

}






void pullLastPcapFile(const std::string &pkgName) {
    std::string listCmd = "adb shell ls /sdcard/Download/PCAPdroid/*.pcap";
    FILE* pipe = popen(listCmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "❌ Failed to run ls command.\n";
        return;
    }

    std::vector<std::string> pcapFiles;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
        if (!line.empty()) {
            pcapFiles.push_back(line);
        }
    }
    pclose(pipe);

    if (pcapFiles.empty()) {
        std::cerr << "❌ No .pcap files found in /sdcard/Download/PCAPdroid/ directory.\n";
        return;
    }

    // Get the last .pcap file
    std::string latestFile = pcapFiles.back();  // Most recent due to filename timestamps
    std::string pullCmd = "adb pull \"" + latestFile + "\" ./results/" + pkgName + ".pcap";
    runCmd(pullCmd);
}

bool findButtonCoordinates(const std::string &buttonText, int &x, int &y) {
    runCmd("adb shell uiautomator dump /sdcard/window.xml");
    runCmd("adb pull /sdcard/window.xml ./window.xml");

    std::ifstream fin("window.xml");
    std::string line;
    bool found = false;
    std::string bounds;
    while (std::getline(fin, line)) {
        if (line.find("text=\"" + buttonText + "\"") != std::string::npos) {
            auto pos = line.find("bounds=");
            if (pos != std::string::npos) {
                auto start = line.find('"', pos) + 1;
                auto end = line.find('"', start);
                bounds = line.substr(start, end - start);
                found = true;
                break;
            }
        }
    }
    fin.close();
    if (!found) return false;

    int x1, y1, x2, y2;
    if (sscanf(bounds.c_str(), "[%d,%d][%d,%d]", &x1, &y1, &x2, &y2) != 4)
        return false;

    x = (x1 + x2) / 2;
    y = (y1 + y2) / 2;
    return true;
}
// Setup the device: factory reset, root, install helper APKs, disable protections
void setupDevice() {

    // 2. Root via Magisk (assuming Magisk APK is on host at ./Magisk.apk)
    runCmd("adb install -r Magisk.apk");

    runCmd("adb shell am start -n com.topjohnwu.magisk/.ui.MainActivity");
    // Wait for rooting to stabilize
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 3. Install PCAPdroid
    runCmd("adb install -r PCAPdroid.apk");

    // 4. Disable Play Protect
    runCmd("adb shell settings put global package_verifier_enable 0");

    // 5. Enable Wi-Fi and connect (modify SSID/PASS if needed)
    runCmd("adb shell svc wifi enable");

    // Note: For open network:
    runCmd("adb shell am start -a android.settings.WIFI_SETTINGS" );
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 6. Push strace binary
    runCmd("adb push strace /data/local/tmp/");
    runCmd("adb shell chmod +x /data/local/tmp/strace");
}


// Perform analysis on a single APK
void analyzeApk(const std::string &apkPath, const std::string &pkgName) {
    // Install the APK under test
  // std::string installCmd = 
   /// "adb install-multiple -r -d " + apkPath +
    //" || (adb uninstall " + pkgName + " && adb install-multiple -r -d " + apkPath + ")";
//runCmd(installCmd);
    // Start PCAPdroid via broadcast intent
   runCmd("adb shell am start -n com.emanuelef.remote_capture/.activities.MainActivity");


std::this_thread::sleep_for(std::chrono::seconds(2));

// Tap the start/capture button
//int startX, startY;
//if (findButtonCoordinates("Ready", startX, startY) || findButtonCoordinates("Capture", startX, startY)) {
  //  std::cout << "🟢 Found Start/Capture button at (" << startX << "," << startY << "). Tapping..." << std::endl;
   // runCmd("adb shell input tap " + std::to_string(startX) + " " + std::to_string(startY));
   // std::this_thread::sleep_for(std::chrono::seconds(5));
//} else {
   // std::cerr << "❌ Could not find Start/Capture button on screen." << std::endl;
//}

runCmd("adb shell input tap 540 811");
std::this_thread::sleep_for(std::chrono::seconds(5));


    // Launch the app
    runCmd("adb shell monkey -p " + pkgName + " -c android.intent.category.LAUNCHER 1");
 runCmd("adb shell monkey -p " + pkgName + " --throttle 500 -v 170");
 // Start Perfetto trace (10s)
    const char *perfettoCmd = R"(adb shell perfetto --txt -o /data/misc/perfetto-traces/trace.perfetto-trace -c - <<EOF
buffers {
  size_kb: 10240
  fill_policy: RING_BUFFER
}
data_sources {
  config {
    name: "linux.ftrace"
    ftrace_config {
      ftrace_events: "sched/sched_switch"
      ftrace_events: "sched/sched_wakeup"
      ftrace_events: "task/task_rename"
      ftrace_events: "binder/binder_transaction"
      ftrace_events: "binder/binder_transaction_received"
      atrace_categories: "webview,input,view,wm,am"
    }
  }
}
duration_ms: 10000
EOF
)";
    runCmd(perfettoCmd);
       // Start strace
      run_strace(pkgName);
    // Stop PCAPdroid via broadcast
    // Bring PCAPdroid to the foreground again
runCmd("adb shell am start -n com.emanuelef.remote_capture/.activities.MainActivity");
std::this_thread::sleep_for(std::chrono::seconds(2));
runCmd("adb shell input tap 774 205");
  std::this_thread::sleep_for(std::chrono::seconds(3));
  runCmd("adb shell input tap 158 1335");
    // Pull other artifacts
        // Pull all artifacts
    runCmd("adb pull /data/misc/perfetto-traces/trace.perfetto-trace ./results/" + pkgName + ".perfetto");
    runCmd("adb pull /data/local/tmp/strace.out ./results/" + pkgName + ".strace");

    pullLastPcapFile(pkgName);

    // Uninstall APK
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <Magisk.apk> <PCAPdroid.apk> <APK1> [<PKG1>] ..." << std::endl;
        return 1;
    }

    std::string magiskApk = argv[1];
    std::string pcapdroidApk = argv[2];

    // Remaining args are pairs of <APK path> <package name>
    std::vector<std::pair<std::string, std::string>> tests;
    for (int i = 3; i + 1 < argc; i += 2) {
        tests.emplace_back(argv[i], argv[i+1]);
    }

    // Initial device setup
    setupDevice();

    // Ensure results directory exists
    runCmd("mkdir -p results");

    // Analyze each APK in sequence
    for (auto &t : tests) {
        analyzeApk(t.first, t.second);
    }

    std::cout << "All analyses completed." << std::endl;
    return 0;
}

