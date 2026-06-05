#include "FlushWorker.h"
#include "SSTable.h"
#include "FileCounter.h"   // wherever filecounter is declared

std::queue<std::shared_ptr<MemTable>> flushQueue;
std::mutex queueMutex;
std::condition_variable cv;
bool shutdown = false;

void writeToDisk(std::shared_ptr<MemTable>& memtable, Levels& levels)
{
    auto sst = std::make_shared<SSTable>();

    std::string filename = filecounter.setfilename("sstable");
    uint64_t filecount = filecounter.setcount();

    sst->filecount = filecount;
    sst->filename = filename;

    sst->writeSST(*memtable);

    levels.addSST(sst);
}

void flushWorker(Levels& levels)
{
    while (true)
    {
        std::shared_ptr<MemTable> memtable;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            cv.wait(lock, []
            {
                return !flushQueue.empty() || shutdown;
            });

            if (shutdown && flushQueue.empty())
                return;

            memtable = flushQueue.front();
            flushQueue.pop();
        }

        writeToDisk(memtable, levels);
    }
}