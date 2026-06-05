#pragma once

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "MemTable.h"
#include "Levels.h"

extern std::queue<std::shared_ptr<MemTable>> flushQueue;
extern std::mutex queueMutex;
extern std::condition_variable cv;
extern bool shutdown;

void writeToDisk(std::shared_ptr<MemTable>& memtable, Levels& levels);
void flushWorker(Levels& levels);