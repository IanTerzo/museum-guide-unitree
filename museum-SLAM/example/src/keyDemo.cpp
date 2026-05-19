#include <unitree/robot/client/client.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>
#include <unitree/idl/ros2/String_.hpp>
#include <json.hpp>
#include <termio.h>
#include <fstream>
#include <string>
#include <future>
#include <thread>
#include <curl/curl.h>

#define SlamInfoTopic "rt/slam_info"
#define SlamKeyInfoTopic "rt/slam_key_info"
using namespace unitree::robot;
using namespace unitree::common;
unsigned char currentKey;

class poseDate
{
public:
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float q_x = 0.0f;
    float q_y = 0.0f;
    float q_z = 0.0f;
    float q_w = 1.0f;
    int mode = 1;
    std::string name;

    std::string toJsonStr() const
    {
        nlohmann::json j;
        j["data"]["targetPose"]["x"] = x;
        j["data"]["targetPose"]["y"] = y;
        j["data"]["targetPose"]["z"] = z;
        j["data"]["targetPose"]["q_x"] = q_x;
        j["data"]["targetPose"]["q_y"] = q_y;
        j["data"]["targetPose"]["q_z"] = q_z;
        j["data"]["targetPose"]["q_w"] = q_w;
        j["data"]["mode"] = mode;
        return j.dump(4);
    }
    void printInfo() const
    {
        std::cout << "x:" << x << " y:" << y << " z:" << z << " q_x:"
                  << q_x << " q_y:" << q_y << " q_z:" << q_z << " q_w:" << q_w << std::endl;
    }
};

namespace unitree::robot::slam
{

    const std::string TEST_SERVICE_NAME = "slam_operate";
    const std::string TEST_API_VERSION = "1.0.0.1";

    const int32_t ROBOT_API_ID_STOP_NODE = 1901;
    const int32_t ROBOT_API_ID_START_MAPPING_PL = 1801;
    const int32_t ROBOT_API_ID_END_MAPPING_PL = 1802;
    const int32_t ROBOT_API_ID_START_RELOCATION_PL = 1804;
    const int32_t ROBOT_API_ID_POSE_NAV_PL = 1102;
    const int32_t ROBOT_API_ID_PAUSE_NAV = 1201;
    const int32_t ROBOT_API_ID_RESUME_NAV = 1202;

    class TestClient : public Client
    {
    private:
        ChannelSubscriberPtr<std_msgs::msg::dds_::String_> subSlamInfo;
        ChannelSubscriberPtr<std_msgs::msg::dds_::String_> subSlamKeyInfo;

        void slamInfoHandler(const void *message);
        void slamKeyInfoHandler(const void *message);

        poseDate curPose;
        std::vector<poseDate> poseList;
        bool is_arrived = false;
        bool threadControl = false;
        std::future<void> futThread;
        std::promise<void> prom;
        std::thread controlThread;

        std::string feedback_ip;
        int feedback_port = 8765;

        void sendFeedbackAndWait(const std::string &pose_name);

    public:
        TestClient();
        ~TestClient();

        void SetFeedbackIp(const std::string &ip, int port = 8765);
        void Init();
        unsigned char keyDetection();
        unsigned char keyExecute();
        void stopNodeFun();
        void startMappingPlFun();
        void endMappingPlFun();
        void relocationPlFun();
        void taskLoopFun(std::promise<void> &prom);
        void pauseNavFun();
        void resumeNavFun();
        void taskThreadRun();
        void taskThreadStop();
        void saveTaskList(const std::string &filename = "/home/unitree/task_list.json");
        void loadTaskList(const std::string &filename = "/home/unitree/task_list.json");
    };

    TestClient::TestClient() : Client(TEST_SERVICE_NAME, false)
    {
        subSlamInfo = ChannelSubscriberPtr<std_msgs::msg::dds_::String_>(new ChannelSubscriber<std_msgs::msg::dds_::String_>(SlamInfoTopic));
        subSlamInfo->InitChannel(std::bind(&unitree::robot::slam::TestClient::slamInfoHandler, this, std::placeholders::_1), 1);
        subSlamKeyInfo = ChannelSubscriberPtr<std_msgs::msg::dds_::String_>(new ChannelSubscriber<std_msgs::msg::dds_::String_>(SlamKeyInfoTopic));
        subSlamKeyInfo->InitChannel(std::bind(&unitree::robot::slam::TestClient::slamKeyInfoHandler, this, std::placeholders::_1), 1);
        std::cout << "***********************  Unitree SLAM Demo (feedback) ***********************\n";
        std::cout << "---------------            q    w                        -----------------\n";
        std::cout << "---------------            a    s   d   f                -----------------\n";
        std::cout << "---------------            z    x                        -----------------\n";
        std::cout << "---------------            e    i                        -----------------\n";
        std::cout << "------------------------------------------------------------------\n";
        std::cout << "------------------ q: Start mapping            -------------------\n";
        std::cout << "------------------ w: End mapping              -------------------\n";
        std::cout << "------------------ a: Start relocation         -------------------\n";
        std::cout << "------------------ s: Add pose to task list    -------------------\n";
        std::cout << "------------------ d: Execute task list        -------------------\n";
        std::cout << "------------------ f: Clear task list          -------------------\n";
        std::cout << "------------------ z: Pause navigation         -------------------\n";
        std::cout << "------------------ x: Resume navigation        -------------------\n";
        std::cout << "------------------ e: Export/save task list    -------------------\n";
        std::cout << "------------------ i: Import/load task list    -------------------\n";
        std::cout << "-------- (poses named 'empty' are skipped entirely) --------------\n";
        std::cout << "---------------- Press any other key to stop SLAM ----------------\n";
        std::cout << "------------------------------------------------------------------\n";
        std::cout << "------------------------------------------------------------------\n";
        std::cout << "--------------- Press 'Ctrl + C' to exit the program -------------\n";
        std::cout << "------------------------------------------------------------------\n";
        std::cout << "------------------------------------------------------------------\n"
                  << std::endl;
    }

    TestClient::~TestClient()
    {
        stopNodeFun();
    }

    void TestClient::SetFeedbackIp(const std::string &ip, int port)
    {
        feedback_ip = ip;
        feedback_port = port;
        std::cout << "[feedback] Will notify " << ip << ":" << port << " on pose arrival" << std::endl;
    }

    void TestClient::Init()
    {
        SetApiVersion(TEST_API_VERSION);

        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_POSE_NAV_PL);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_PAUSE_NAV);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_RESUME_NAV);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_STOP_NODE);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_START_MAPPING_PL);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_END_MAPPING_PL);
        UT_ROBOT_CLIENT_REG_API_NO_PROI(ROBOT_API_ID_START_RELOCATION_PL);
    }
}

static size_t curlWriteDiscard(void * /*ptr*/, size_t size, size_t nmemb, void * /*userdata*/)
{
    return size * nmemb;
}

void unitree::robot::slam::TestClient::sendFeedbackAndWait(const std::string &pose_name)
{
    if (feedback_ip.empty())
        return;

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        std::cerr << "[feedback] curl_easy_init failed" << std::endl;
        return;
    }

    std::string url = "http://" + feedback_ip + ":" + std::to_string(feedback_port) + "/arrived";
    std::string body = "{\"pose_name\":\"" + pose_name + "\"}";

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteDiscard);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    std::cout << "[feedback] Arrived at \"" << pose_name << "\", waiting for feedback server..." << std::endl;

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        std::cerr << "[feedback] curl error: " << curl_easy_strerror(res) << std::endl;
    else
        std::cout << "[feedback] Feedback received, continuing to next pose." << std::endl;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void unitree::robot::slam::TestClient::saveTaskList(const std::string &filename)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto &p : poseList)
    {
        nlohmann::json item;
        item["name"] = p.name;
        item["x"] = p.x;
        item["y"] = p.y;
        item["z"] = p.z;
        item["q_x"] = p.q_x;
        item["q_y"] = p.q_y;
        item["q_z"] = p.q_z;
        item["q_w"] = p.q_w;
        item["mode"] = p.mode;
        j.push_back(item);
    }
    std::ofstream f(filename);
    if (!f.is_open())
    {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return;
    }
    f << j.dump(4);
    std::cout << "Task list saved to \"" << filename << "\" (" << poseList.size() << " poses)" << std::endl;
}

void unitree::robot::slam::TestClient::loadTaskList(const std::string &filename)
{
    std::ifstream f(filename);
    if (!f.is_open())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to parse task list: " << e.what() << std::endl;
        return;
    }
    poseList.clear();
    for (const auto &item : j)
    {
        poseDate p;
        p.name = item["name"].get<std::string>();
        p.x = item["x"].get<float>();
        p.y = item["y"].get<float>();
        p.z = item["z"].get<float>();
        p.q_x = item["q_x"].get<float>();
        p.q_y = item["q_y"].get<float>();
        p.q_z = item["q_z"].get<float>();
        p.q_w = item["q_w"].get<float>();
        p.mode = item["mode"].get<int>();
        poseList.push_back(p);
    }
    std::cout << "Task list loaded from \"" << filename << "\" (" << poseList.size() << " poses)" << std::endl;
    for (size_t i = 0; i < poseList.size(); i++)
        std::cout << "  [" << i << "] " << poseList[i].name << std::endl;
}

void unitree::robot::slam::TestClient::taskThreadRun()
{
    taskThreadStop();
    prom = std::promise<void>();
    futThread = prom.get_future();
    controlThread = std::thread(&unitree::robot::slam::TestClient::taskLoopFun, this, std::ref(prom));
    controlThread.detach();
}

void unitree::robot::slam::TestClient::taskLoopFun(std::promise<void> &prom)
{
    std::string data;
    threadControl = true;
    std::cout << "task list num:" << poseList.size() << std::endl;

    for (int i = 0; i < (int)poseList.size(); i++)
    {
        if (poseList[i].name == "empty")
        {
            std::cout << "Skipping empty task at index " << i << std::endl;
        }
        else
        {
            is_arrived = false;
            int32_t statusCode = Call(ROBOT_API_ID_POSE_NAV_PL, poseList[i].toJsonStr(), data);
            std::cout << "parameter:" << poseList[i].toJsonStr() << std::endl;
            std::cout << "statusCode:" << statusCode << std::endl;
            std::cout << "data:" << data << std::endl;

            while (!is_arrived)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (!threadControl)
                    break;
            }

            if (is_arrived && !feedback_ip.empty())
            {
                sendFeedbackAndWait(poseList[i].name);
            }
        }

        if (i == (int)poseList.size() - 1)
        {
            i = 0;
            std::reverse(poseList.begin(), poseList.end());
        }
        if (!threadControl)
            break;
    }

    prom.set_value();
}

void unitree::robot::slam::TestClient::taskThreadStop()
{
    threadControl = false;
    if (futThread.valid())
    {
        auto status = futThread.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready)
            futThread.wait();
    }
}

void unitree::robot::slam::TestClient::slamInfoHandler(const void *message)
{
    std_msgs::msg::dds_::String_ currentMsg = *(std_msgs::msg::dds_::String_ *)message;
    nlohmann::json jsonData = nlohmann::json::parse(currentMsg.data());

    if (jsonData["errorCode"] != 0)
    {
        std::cout << "\033[33m" << jsonData["info"] << "\033[0m" << std::endl;
        return;
    }

    if (jsonData["type"] == "pos_info")
    {
        curPose.x = jsonData["data"]["currentPose"]["x"];
        curPose.y = jsonData["data"]["currentPose"]["y"];
        curPose.z = jsonData["data"]["currentPose"]["z"];
        curPose.q_x = jsonData["data"]["currentPose"]["q_x"];
        curPose.q_y = jsonData["data"]["currentPose"]["q_y"];
        curPose.q_z = jsonData["data"]["currentPose"]["q_z"];
        curPose.q_w = jsonData["data"]["currentPose"]["q_w"];
    }
}

void unitree::robot::slam::TestClient::slamKeyInfoHandler(const void *message)
{
    std_msgs::msg::dds_::String_ currentMsg = *(std_msgs::msg::dds_::String_ *)message;
    nlohmann::json jsonData = nlohmann::json::parse(currentMsg.data());

    if (jsonData["errorCode"] != 0)
    {
        std::cout << "\033[33m" << jsonData["info"] << "\033[0m" << std::endl;
        return;
    }

    if (jsonData["type"] == "task_result")
    {
        is_arrived = jsonData["data"]["is_arrived"];
        if (is_arrived)
            std::cout << "I arrived " << jsonData["data"]["targetNodeName"] << std::endl;
        else
            std::cout << "I not arrived " << jsonData["data"]["targetNodeName"] << "  Please help me!!  (T_T)   (T_T)   (T_T) " << std::endl;
    }
}

void unitree::robot::slam::TestClient::stopNodeFun()
{
    std::string parameter, data;
    parameter = R"({"data": {}})";
    int32_t statusCode = Call(ROBOT_API_ID_STOP_NODE, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

void unitree::robot::slam::TestClient::startMappingPlFun()
{
    std::string parameter, data;
    parameter = R"({"data": {"slam_type": "indoor"}})";
    int32_t statusCode = Call(ROBOT_API_ID_START_MAPPING_PL, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

void unitree::robot::slam::TestClient::endMappingPlFun()
{
    std::string parameter, data;
    parameter = R"({"data": {"address": "/home/unitree/test.pcd"}})";
    int32_t statusCode = Call(ROBOT_API_ID_END_MAPPING_PL, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

void unitree::robot::slam::TestClient::relocationPlFun()
{
    std::string parameter, data;
    parameter =
        R"({
        "data": {
            "x": 0.0,
            "y": 0.0,
            "z": 0.0,
            "q_x": 0.0,
            "q_y": 0.0,
            "q_z": 0.0,
            "q_w": 1.0,
            "address": "/home/unitree/test.pcd"
        }
        })";
    int32_t statusCode = Call(ROBOT_API_ID_START_RELOCATION_PL, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

void unitree::robot::slam::TestClient::pauseNavFun()
{
    std::string parameter, data;
    parameter = R"({"data": {}})";
    int32_t statusCode = Call(ROBOT_API_ID_PAUSE_NAV, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

void unitree::robot::slam::TestClient::resumeNavFun()
{
    std::string parameter, data;
    parameter = R"({"data": {}})";
    int32_t statusCode = Call(ROBOT_API_ID_RESUME_NAV, parameter, data);
    std::cout << "statusCode:" << statusCode << std::endl;
    std::cout << "data:" << data << std::endl;
}

unsigned char unitree::robot::slam::TestClient::keyDetection()
{
    termios tms_old, tms_new;
    tcgetattr(0, &tms_old);
    tms_new = tms_old;
    tms_new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &tms_new);
    unsigned char ch = getchar();
    tcsetattr(0, TCSANOW, &tms_old);
    std::cout << "\033[1;32m"
              << "Key " << ch << " pressed."
              << "\033[0m" << std::endl;
    return ch;
}

unsigned char unitree::robot::slam::TestClient::keyExecute()
{
    unsigned char currentKey;
    while (true)
    {
        currentKey = keyDetection();
        switch (currentKey)
        {
        case 'q':
            startMappingPlFun();
            break;
        case 'w':
            endMappingPlFun();
            break;
        case 'a':
            relocationPlFun();
            break;
        case 's':
        {
            poseDate newPose = curPose;
            std::cout << "Enter pose name: " << std::flush;
            std::string poseName;
            std::getline(std::cin, poseName);
            if (poseName.empty())
                poseName = "pose_" + std::to_string(poseList.size() + 1);
            newPose.name = poseName;
            newPose.printInfo();
            poseList.push_back(newPose);
            std::cout << "Pose \"" << poseName << "\" added. Task list size: " << poseList.size() << std::endl;
            break;
        }
        case 'd':
            taskThreadRun();
            break;
        case 'f':
            poseList.clear();
            std::cout << "Clear task list" << std::endl;
            break;
        case 'z':
            pauseNavFun();
            break;
        case 'x':
            resumeNavFun();
            break;
        case 'e':
        {
            std::cout << "Enter filename to save (leave empty for default /home/unitree/task_list.json): " << std::flush;
            std::string fname;
            std::getline(std::cin, fname);
            if (fname.empty())
                fname = "/home/unitree/task_list.json";
            saveTaskList(fname);
            break;
        }
        case 'i':
        {
            std::cout << "Enter filename to load (leave empty for default /home/unitree/task_list.json): " << std::flush;
            std::string fname;
            std::getline(std::cin, fname);
            if (fname.empty())
                fname = "/home/unitree/task_list.json";
            loadTaskList(fname);
            break;
        }
        default:
            taskThreadStop();
            stopNodeFun();
            break;
        }
    }
}

int main(int argc, const char **argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " networkInterface [--feedback_ip=<ip>] [--feedback_port=<port>]" << std::endl;
        exit(-1);
    }

    std::string feedback_ip_arg;
    int feedback_port_arg = 8765;
    for (int i = 2; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg.rfind("--feedback_ip=", 0) == 0)
            feedback_ip_arg = arg.substr(14);
        else if (arg.rfind("--feedback_port=", 0) == 0)
            feedback_port_arg = std::stoi(arg.substr(16));
    }

    unitree::robot::ChannelFactory::Instance()->Init(0, argv[1]);
    unitree::robot::slam::TestClient tc;

    if (!feedback_ip_arg.empty())
        tc.SetFeedbackIp(feedback_ip_arg, feedback_port_arg);

    tc.Init();
    tc.SetTimeout(10.0f);
    tc.keyExecute();
    return 0;
}
