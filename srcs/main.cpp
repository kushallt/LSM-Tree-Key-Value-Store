#include "MemTable.h"
#include "SSTable.h"
#include "Levels.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>
#include "flushWorker.h"

void log_performance(double num, std::ofstream &file, std::string text)
{
    if (file.is_open())
    {
        // Use fixed and setprecision to ensure you see the milliseconds
        // Example: 12.345678
        file << text << num << std::endl;
    }
}


void run()
{

    std::shared_ptr<MemTable> memtable = std::make_shared<MemTable>();
    Levels levels;
    levels.initialize();
    std::string key, value;
    std::ofstream benchmarkfile("benchmark_results.csv", std::ios::app);

    int ind = 0;
    std::string val = "kushal";
    std::string keytest;
    auto writestart = std::chrono::high_resolution_clock::now();
    int numWriteOperations = 10000;

    std::thread workerThread(flushWorker, std::ref(levels));

    while (ind < numWriteOperations)
    {
        std::clog<<"\rind"<<ind<<std::flush;
        std::ostringstream oss;
        oss << std::setw(6) << std::setfill('0') << ind;
        keytest = oss.str();
        memtable->insertElement(keytest, val);
        if (memtable->size >= memtable->maxsize)
        {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                flushQueue.push(memtable);
                memtable = std::make_shared<MemTable>();
            }
            cv.notify_one();
        }

        // std::cout<<"added element :"<<ind<<std::endl;
        ind++;
    }
    std::cout<<"partial writing done."<<std::endl;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        flushQueue.push(memtable);
        shutdown = true;
    }
    cv.notify_one();
    workerThread.join();
    std::cout<<"writing completely to disk done"<<std::endl;

    auto writeend = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = writeend - writestart;
    double throughput = numWriteOperations / duration.count();
    // log_performance(throughput, benchmarkfile, "write throughput(in writes/sec) ");

    int numreads = 10000;
    auto readstart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numreads; i++)
    {
        std::ostringstream oss;
        oss << std::setw(6) << std::setfill('0') << i;
        keytest = oss.str();
        levels.searchLevels(keytest);
    }
    auto readend = std::chrono::high_resolution_clock::now();
    ;
    duration = readend - readstart;
    double readlatency = numreads / duration.count();
    // log_performance(readlatency, benchmarkfile, "read latency(in reads/sec) ");

    int lv = 0;
    for (auto &v : levels.levelsInfo)
    {
        std::cout << "level :" << lv << std::endl;
        for (std::shared_ptr<SSTable> s : v)
        {
            std::cout << s->filename << std::endl;
        }
        lv++;
    }
    // bool cont;
    // int add;
    // while (true)
    // {
    //     std::cout << "Do you want to continue (1/0)?" << std::endl;
    //     std::cin >> cont;
    //     if (!cont)
    //         break;

    //     std::cout << "Do you want to search/add/delete value (2/1/0)?" << std::endl;
    //     std::cin >> add;

    //     if (add == 1)
    //     {
    //         std::cout << "Enter key: " << std::endl;
    //         std::cin >> key;

    //         std::cout << "Enter value: " << std::endl;
    //         std::cin >> value;

    //         memtable->insertElement(key, value);
    //         if (memtable->size >= memtable->maxsize)
    //         {
    //             SSTable *sst = new SSTable;
    //             sst->writeSST(*memtable);
    //             levels.addSST(sst);
    //             memtable.reset();
    //             memtable = std::make_unique<MemTable>();
    //         }
    //     }
    //     else if (add == 0)
    //     {
    //         std::cout << "Enter key:" << std::endl;
    //         std::cin >> key;
    //         value = "";
    //         memtable->insertElement(key, value, 1);
    //         if (memtable->size >= memtable->maxsize)
    //         {
    //             SSTable *sst = new SSTable;
    //             sst->writeSST(*memtable);
    //             levels.addSST(sst);
    //             memtable.reset();
    //             memtable = std::make_unique<MemTable>();
    //         }
    //     }
    //     else if (add == 2)
    //     {
    //         std::cout << "Enter key:" << std::endl;
    //         std::cin >> key;

    //         if (memtable->getElement(key).size() == 0)
    //         {
    //             std::cout << levels.searchLevels(key) << std::endl;
    //         }
    //         else
    //         {
    //             std::cout << memtable->getElement(key) << std::endl;
    //         }
    //     }
    //     std::cout<<memtable->size<<std::endl;
    // }

    benchmarkfile.close();
}

int main()
{
    run();
    // MemTable memtable = MemTable();
    // memtable.insertElement("student1", "kushal");
    // memtable.insertElement("student2", "kushal");
    // memtable.insertElement("student3", "kushal");
    // memtable.insertElement("student4", "kushal");
    // memtable.insertElement("student5", "kushal");
    // memtable.insertElement("student6", "kushal");
    // memtable.insertElement("student7", "kushal");
    // memtable.insertElement("student8", "kushal");
    // memtable.insertElement("student9", "kushal");
    // SSTable sst;
    // sst.filename = filecounter.setfilename("sstable");
    // sst.filecount = filecounter.setcount();
    // sst.writeSST(memtable);

    return 0;
}