#ifndef _CHAOS_MISC_DEF_H_
#define _CHAOS_MISC_DEF_H_

//! yunjie: 日志模块定义
#ifndef LOG_MODULE
#define TEST_MODULE                 "TEST_MODULE"
#endif

#define LOCALHOST       "127.0.0.1"

//! yunjie: 所有service对象, 注: service类不要使用单例, 因为static变量析构顺序的不确定性和依赖性, 会导致关闭进程时crash

task_service_t*              task_service_1104;
work_service_t*              work_service_1104;
work_service_group_t*        work_service_group_1104;
task_service_t*              log_service_1104;

#define NEW_SERVICE() \
    task_service_1104 = new task_service_t("global task service"); \
    work_service_1104 = new work_service_t("global work service"); \
    work_service_group_1104 = new work_service_group_t(); \
    log_service_1104 = new task_service_t("log service");

#define DEL_SERVICE() \
    SAFE_DELETE(task_service_1104); \
    SAFE_DELETE(work_service_1104); \
    SAFE_DELETE(work_service_group_1104); \
    SAFE_DELETE(log_service_1104);
        
#define TS()    (*task_service_1104)
#define WS()    (*work_service_1104)
#define LOGS()  (*log_service_1104)
#define WSG()   (*work_service_group_1104)


//! yunjie: 日志模块封装
class log_tool_t
{
public:
    static void print_screen_callback(const std::string& msg_)
    {
        if (is_started)
        {
            LOGS().post(async_method_t::bind_memfunc(&singleton_t<log_t>::instance(), &log_t::handle_print_screen, msg_));
        }
    }

    static void print_file_callback(const std::string& msg_)
    {
        if (is_started)
        {
            LOGS().post(async_method_t::bind_memfunc(&singleton_t<log_t>::instance(), &log_t::handle_print_file, msg_));
        }
    }

    static int start_log_service(const char* log_path_, int log_level_, int screen_, int file_)
    {
        string log_path = log_path_;
        string log_name = "log";
        vector<string> modules;
        modules.push_back(TEST_MODULE);
        //! modules.push_back(THREAD_MODULE);
        modules.push_back(TASK_SERVICE_MODULE);
        modules.push_back(TIMER_MANAGER_MODULE);
        modules.push_back(IO_MULTIPLEX_MODULE);
        modules.push_back(WORK_SERVICE_MODULE);
        modules.push_back(ACCEPTOR_SERVICE_MODULE);
        modules.push_back(CONNECTION_MODULE);
        modules.push_back(HEART_BEAT_MOUDLE);

        init_log(log_path,
                log_name,
                file_,                              //! yunjie: 是否打印到文件
                screen_,                            //! yunjie: 是否输出到屏幕
                log_level_,                         //! yunjie: log level
                modules,
                print_screen_callback,
                print_file_callback
                );

        LOGS().start(1);
        is_started = true;

        return 0;
    }

    static int stop_log_service()
    {
        LOGS().stop();
        is_started = false;

        return 0;
    }

private:
    static bool             is_started;
};

bool log_tool_t::is_started = false;

class application_tool_t
{
public:

    static int block_all_signal()
    {
        singleton_t<signal_handler_t>::instance().block_all_signal();

        return 0;
    }

    //! yunjie: 信号注册及监听内核信号队列
    static int wait_signal()
    {
        singleton_t<signal_handler_t>::instance().register_quit_signal(SIGINT);
        singleton_t<signal_handler_t>::instance().register_quit_signal(SIGQUIT);
        singleton_t<signal_handler_t>::instance().register_quit_signal(SIGTERM);
        singleton_t<signal_handler_t>::instance().register_quit_signal(SIGHUP);
        singleton_t<signal_handler_t>::instance().event_loop();     //! event loop to wait for signal

        return 0;
    }
};




#endif //! _CHAOS_MISC_DEF_H_
