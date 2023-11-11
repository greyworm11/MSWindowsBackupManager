#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <Windows.h>
#include <TCHAR.H>
#include <winsvc.h>
#include <errhandlingapi.h>
#include <zip.h>
#include <sys/stat.h>
#include <map>

#define BUF_SIZE 4096 * 64

const std::string currentDateTime();

SERVICE_STATUS serviceStatus;
SERVICE_STATUS_HANDLE hStatus;

constexpr LPWSTR serviceName = (LPWSTR)TEXT("MS Windows Backup Manager");
constexpr LPWSTR servicePath = (LPWSTR)TEXT("C:/Users/serge/source/repos/БСИТ_2/x64/Debug/БСИТ_2.exe");

constexpr auto logPath = "C:/Users/serge/Documents/log.txt";
constexpr auto configPath = "C:/Users/serge/Documents/config.txt";  

const auto archiveName = "backup_" + currentDateTime() + ".zip";

std::map<std::string, std::filesystem::file_time_type> fileTimes = {};
std::map<std::string, unsigned int> fileCrc = {};

unsigned int CRC32_function(unsigned char* buf, unsigned long len)
{
    unsigned long crc_table[256];
    unsigned long crc;
    for (int i = 0; i < 256; i++)
    {
        crc = i;
        for (int j = 0; j < 8; j++)
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        crc_table[i] = crc;
    };
    crc = 0xFFFFFFFFUL;
    while (len--)
        crc = crc_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

//возвращание конечного CRC32. Достаточно вызвать эту функцию и указать имя файла, для которого будет произведён расчёт
unsigned int CRC32_count(const char* filename)
{
    char buf[BUF_SIZE];
    std::ifstream f(filename, std::ios::binary);
    f.read(buf, BUF_SIZE);
    return CRC32_function((unsigned char*)buf, f.gcount());
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string currentDateTime()
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y_%m_%d__%H_%M_%S", &tstruct);

    return buf;
}


int addLogMessage(const std::string& str)
{
    static auto count = 0;

    std::ofstream output(logPath, std::ios_base::app);
    if (output.fail())
        return -1;

    output << "[" << ++count << "]" << ": " << str << std::endl;

    output.close();
    return 1;
}

int InstallService()
{
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCManager)
        return addLogMessage("unable to open the SCM(Service Control Manager)...");

    SC_HANDLE hService = CreateService(
        hSCManager,
        serviceName,
        serviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        servicePath,
        nullptr, nullptr, nullptr, nullptr, nullptr
    );
    if (!hService)
    {
        switch (GetLastError())
        {
        case ERROR_ACCESS_DENIED:
            addLogMessage("error: ERROR_ACCESS_DENIED...");
            break;
        case ERROR_CIRCULAR_DEPENDENCY:
            addLogMessage("error: ERROR_CIRCULAR_DEPENDENCY...");
            break;
        case ERROR_DUPLICATE_SERVICE_NAME:
            addLogMessage("error: ERROR_DUPLICATE_SERVICE_NAME...");
            break;
        case ERROR_INVALID_HANDLE:
            addLogMessage("error: ERROR_INVALID_HANDLE...");
            break;
        case ERROR_INVALID_NAME:
            addLogMessage("error: ERROR_INVALID_NAME...");
            break;
        case ERROR_INVALID_PARAMETER:
            addLogMessage("error: ERROR_INVALID_PARAMETER...");
            break;
        case ERROR_INVALID_SERVICE_ACCOUNT:
            addLogMessage("error: ERROR_INVALID_SERVICE_ACCOUNT...");
            break;
        case ERROR_SERVICE_EXISTS:
            addLogMessage("error: ERROR_SERVICE_EXISTS...");
            break;
        default:
            addLogMessage("error: Undefined...");
        }
        CloseServiceHandle(hSCManager);
        return -1;
    }
    CloseServiceHandle(hService);

    CloseServiceHandle(hSCManager);
    addLogMessage("service installed successfully!");
    std::cout << "service installed successfully!" << std::endl;

    return 0;
}

int RemoveService()
{
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager)
        return addLogMessage("unable to open the SCM(Service Control Manager)...");

    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_STOP | DELETE);
    if (!hService)
    {
        addLogMessage("error: can't remove service...");
        CloseServiceHandle(hSCManager);
        return -1;
    }

    DeleteService(hService);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    addLogMessage("service removed successfully!");
    std::cout << "service removed successfully!" << std::endl;

    return 0;
}

int StartService()
{
    std::cout << "log file location: '" << logPath << "'. ";
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_START);

    if (!StartService(hService, 0, nullptr))
    {
        CloseServiceHandle(hSCManager);
        std::cout << "error :" << GetLastError() << std::endl;

        return addLogMessage("unable to start the service...");
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    std::cout << "service started!" << std::endl;

    return 0;
}

void ControlHandler(DWORD request)
{
    switch (request)
    {
    case SERVICE_CONTROL_STOP:
        addLogMessage("stopping request...");
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &serviceStatus);
        return;
    case SERVICE_CONTROL_SHUTDOWN:
        addLogMessage("shutdown request...");
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &serviceStatus);
        return;

    default:
        break;
    }

    SetServiceStatus(hStatus, &serviceStatus);
}



std::string applyForRegex(std::string mask)
{
    if (mask.empty()) throw std::runtime_error("error: incorrect mask...");
    for (long ind = mask.find('?'); ind != std::string::npos; ind = mask.find('?', ++ind)) mask.replace(ind, 1, ".");
    for (long ind = mask.find('*'); ind != std::string::npos; ind = mask.find('*', ++ind)) mask.replace(ind++, 1, ".*");

    return mask;
}



std::filesystem::path root_out(const std::filesystem::path& p)
{
    auto rel = p.relative_path();
    if (rel.empty()) return {};

    return rel.lexically_relative(*rel.begin());
}


int proceedArchivation(const std::filesystem::path& Directory, const std::filesystem::path& Archive,
                       const std::vector<std::string>& masks)
{
    try
    {
        int error = 0;
        zip* arch = zip_open(Archive.string().c_str(), ZIP_TRUNCATE, &error); // Create the archive if it does not exist
        if (arch == nullptr)
        {
            std::cout << "unable to open the archive... check the archive filename and path" << std::endl;
            return addLogMessage("unable to open the archive... check the archive filename and path");
        }

        std::vector<std::regex> filters;
        filters.reserve(masks.size());
        for (const auto& mask : masks)
        {
            filters.push_back(std::regex(mask));
        }

        for (const auto& item : std::filesystem::recursive_directory_iterator(Directory))
        {
            const auto pathInArchive = std::filesystem::relative(root_out(item.path()), root_out(Directory));
            if (item.is_directory())
            {
                if (auto iter = fileTimes.find(item.path().filename().string()); iter != fileTimes.end())
                { // если для папки сохранено время последнего изменения
                    //addLogMessage("found " + item.path().filename().string() + "!");
                    // получаем последнее время изменения
                    const auto lastWriteTime = std::filesystem::last_write_time(item.path());
                    if (fileTimes.find(item.path().string()) != fileTimes.end() && fileTimes[item.path().string()] == lastWriteTime)
                    {
                        // папка не изменена, пропускаем
                        addLogMessage("folder " + item.path().filename().string() + " has not been modified, skipping");
                        continue;
                    }
                    else
                    {
                        // папка изменена, добавляем ее заново и изменяем время модификации папки
                        addLogMessage("folder " + item.path().filename().string() + " has been modified");
                        fileTimes[item.path().filename().string()] = lastWriteTime;
                        auto result = zip_dir_add(arch, pathInArchive.string().c_str(), ZIP_FL_ENC_GUESS);
                        continue;
                    }
                }
                else
                {
                    // addLogMessage("not found " + item.path().filename().string());
                    addLogMessage("add folder " + item.path().filename().string() + " for the first time");
                    fileTimes[item.path().filename().string()] = std::filesystem::last_write_time(item.path());
                    // папка еще не добавлена в список, добавляем в список и в архив
                    auto result = zip_dir_add(arch, pathInArchive.string().c_str(), ZIP_FL_ENC_GUESS);
                    continue;
                }
            }

            if (item.is_regular_file())
            {
                const std::string filename = item.path().filename().string();

                if (auto iter = fileTimes.find(filename); iter != fileTimes.end())
                {
                    //addLogMessage("found " + filename + "!");
                    // получаем последнее время изменения
                    const auto lastWriteTime = std::filesystem::last_write_time(item.path());
                    if (fileTimes.find(item.path().string()) != fileTimes.end() && fileTimes[item.path().string()] == lastWriteTime)
                    {
                        // файл не изменен, пропускаем
                        addLogMessage("file " + filename + " already in archive, checking next");
                        continue;
                    }
                    else
                    {
                        // файл изменен
                        // проверяем совпадение маске
                        bool flag = true;
                        for (const auto& filter : filters)
                        {
                            std::smatch what;
                            if (std::regex_match(filename, what, filter))
                            {
                                flag = true;
                                break;
                            }
                            else flag = false;
                        }
                        // если маска не совпадает, то пропускаем
                        if (flag == false) continue;
                        // если маска совпадает, то добавляем
                        addLogMessage("file " + filename + " has been modified, updating on archive");
                        auto* source = zip_source_file(arch, item.path().string().c_str(), 0, 0);
                        if (source == nullptr) return addLogMessage("source filename creating error...");

                        auto result = zip_file_add(arch, pathInArchive.string().c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
                        if (result < 0) return addLogMessage("unable to add item '" + pathInArchive.string() + "' to the archive...");
                        // обновляем время модификации файла
                        fileTimes[item.path().string()] = lastWriteTime;
                    }
                }
                else
                {
                    // файл не найден в списке
                    // доблавяем в список
                    fileTimes[filename] = std::filesystem::last_write_time(item.path());
                    // проверяем совпадение маске
                    bool flag = true;
                    for (const auto& filter : filters)
                    {
                        std::smatch what;
                        if (std::regex_match(filename, what, filter))
                        {
                            flag = true;
                            break;
                        }
                        else flag = false;
                    }
                    // если маска не совпадает, то пропускаем
                    if (flag == false) continue;
                    // если маска совпадает, то добавляем
                    addLogMessage("add " + filename + " for the first time");
                    auto* source = zip_source_file(arch, item.path().string().c_str(), 0, 0);
                    if (source == nullptr) return addLogMessage("source filename creating error...");

                    auto result = zip_file_add(arch, pathInArchive.string().c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
                    if (result < 0) return addLogMessage("unable to add item '" + pathInArchive.string() + "' to the archive...");
                }
            }
        }

        zip_close(arch);
    }
    catch (std::exception& e)
    {
        addLogMessage("unknown exception while archiving: " + std::string(e.what()) + "...");
    }

    return 0;
}

void ServiceMain(int argc, char** argv)
{
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    serviceStatus.dwWin32ExitCode = 0;
    serviceStatus.dwServiceSpecificExitCode = 0;
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwWaitHint = 0;

    hStatus = RegisterServiceCtrlHandler(serviceName, (LPHANDLER_FUNCTION)ControlHandler);

    if (hStatus == (SERVICE_STATUS_HANDLE) nullptr) return;

    serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(hStatus, &serviceStatus);

    while (serviceStatus.dwCurrentState == SERVICE_RUNNING)
    {
        std::ifstream config(configPath);
        if (config.fail())
        {
            addLogMessage("unable to find the configuration file...");

            serviceStatus.dwCurrentState = SERVICE_STOPPED;
            serviceStatus.dwWin32ExitCode = -1;
            SetServiceStatus(hStatus, &serviceStatus);
            return;
        }

        std::string backupDirectoryStr;
        std::string archiveLocationStr;

        std::getline(config, backupDirectoryStr);
        std::getline(config, archiveLocationStr);

        std::filesystem::path Directory;
        std::filesystem::path ArchiveLocation;

        try
        {
            Directory = std::filesystem::path(backupDirectoryStr);
        }
        catch (std::invalid_argument& e)
        {
            const std::string what = "error: invalid backup directory " + backupDirectoryStr + "...";
            addLogMessage(what.c_str());
            continue;
        }

        try
        {
            ArchiveLocation = std::filesystem::path(archiveLocationStr + "\\" + archiveName);
        }
        catch (std::invalid_argument& e)
        {
            const std::string what = "error: invalid archive location " + archiveLocationStr + "...";
            addLogMessage(what.c_str());
            continue;
        }

        std::string masksCountStr;
        std::getline(config, masksCountStr);
        size_t masksCount = 0;

        try
        {
            masksCount = std::stoi(masksCountStr);
        }
        catch (const std::invalid_argument& e)
        {
            addLogMessage("incorrect masks number in config file... should be number in third string of config file");
            continue;
        }
        std::vector<std::string> masks;
        masks.reserve(masksCount);

        std::string temp;
        for (auto i = 0; i < masksCount; ++i)
        {
            std::getline(config, temp);
            if (temp.empty())
                addLogMessage("mask is empty... try correct one");
            else
                masks.push_back(applyForRegex(temp));
        }

        addLogMessage("backup directory: '" + backupDirectoryStr + "'.");
        addLogMessage("archive location: '" + archiveLocationStr + "\\" + archiveName + "'.");
        addLogMessage(std::to_string(masksCount) + " masks");
        proceedArchivation(Directory, ArchiveLocation, masks);

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

}


int StopService()
{
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP);

    if (QueryServiceStatus(hService, &serviceStatus))
    {
        if (serviceStatus.dwCurrentState == SERVICE_RUNNING) ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus);
        else return addLogMessage("unable to stop the service...");
    }

    addLogMessage("service stopped successfully!");
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    std::cout << "service stopped successfully!" << std::endl;

    return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
    SERVICE_TABLE_ENTRY ServiceTable[1];
    ServiceTable[0].lpServiceName = serviceName;
    ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;
    StartServiceCtrlDispatcher(ServiceTable);

    if (argc != 1)
    {
        if (wcscmp(argv[argc - 1], _T("install")) == 0) InstallService();
        if (wcscmp(argv[argc - 1], _T("remove")) == 0) RemoveService();
        if (wcscmp(argv[argc - 1], _T("start")) == 0) StartService();
        if (wcscmp(argv[argc - 1], _T("stop")) == 0) StopService();
    }
}
