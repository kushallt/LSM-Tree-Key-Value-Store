#include "Levels.h"

void Levels::compact(int l1)
{
    if (l1 >= maxlevels)
        return;
    std::shared_ptr<SSTable> currsst = levelsInfo[l1].back();
    std::string currstartkey = currsst->startkey;
    std::string currendkey = currsst->endkey;
    std::priority_queue<heapNode, std::vector<heapNode>, std::greater<heapNode>> entrypq;

    auto rangesOverlap = [&](const std::string &s_start, const std::string &s_end)
    {
        return !(s_end < currstartkey || s_start > currendkey);
    };

    std::shared_ptr<SSTable> s;
    std::vector<std::shared_ptr<SSTable>> nextLevelKeepers;
    for (size_t i = 0; i < levelsInfo[l1 + 1].size(); i++)
    {
        s = levelsInfo[l1 + 1][i];
        if (s && rangesOverlap(s->startkey, s->endkey))
        {
            SSTtoPq(s, entrypq, l1 + 1);
            sizeofLevels[l1 + 1] -= s->getfilesize();
            if (std::filesystem::exists(s->filename))
            {
                std::filesystem::remove(s->filename);
            }
        }
        else
        {
            nextLevelKeepers.push_back(s);
        }
    }
    levelsInfo[l1 + 1] = std::move(nextLevelKeepers);
    std::vector<std::shared_ptr<SSTable>> currLevelKeepers;
    for (size_t i = 0; i < levelsInfo[l1].size(); i++)
    {
        s = levelsInfo[l1][i];
        if (s && rangesOverlap(s->startkey, s->endkey))
        {
            SSTtoPq(s, entrypq, l1);
            sizeofLevels[l1] -= s->getfilesize();
            if (std::filesystem::exists(s->filename))
            {
                std::filesystem::remove(s->filename);
            }
        }
        else
        {
            currLevelKeepers.push_back(s);
        }
    }
    levelsInfo[l1] = std::move(currLevelKeepers);
    if (l1 != 0)
    {
        std::sort(levelsInfo[l1].begin(), levelsInfo[l1].end(), [](const std::shared_ptr<SSTable> a, const std::shared_ptr<SSTable> b)
                  {
                    if (!a) return false;
                    if (!b) return true;
                    
                    return a->startkey < b->startkey; });
    }

    MemTable tempmemtable = MemTable();
    std::string lastinsert = "";
    bool firstinsert = true;

    while (!entrypq.empty())
    {
        heapNode last = entrypq.top();

        if (!firstinsert && entrypq.top().key == lastinsert)

            continue;
        entrypq.pop();
        tempmemtable.insertElement(last.key, last.value);
        lastinsert = last.key;
        firstinsert = false;
    }
    if (!tempmemtable.isEmpty())
    {
        std::string newfilename = filecounter.setfilename("sstable");
        int newfilecount = filecounter.setcount();

        auto newsst = std::make_shared<SSTable>();
        newsst->filecount = newfilecount;
        newsst->filename = newfilename;

        newsst->writeSST(tempmemtable);
        levelsInfo[l1 + 1].push_back(newsst);
        sizeofLevels[l1 + 1] += newsst->getfilesize();
        std::sort(levelsInfo[l1 + 1].begin(), levelsInfo[l1 + 1].end(), [](const std::shared_ptr<SSTable> a, const std::shared_ptr<SSTable> b)
                  {
                    if (!a) return false;
                    if (!b) return true;
                    
                    return a->startkey < b->startkey; });
    }
    else
    {
        if (tempmemtable.walstream.is_open())
            tempmemtable.walstream.close();
        if (std::filesystem::exists(tempmemtable.wal))
        {
            std::filesystem::remove(tempmemtable.wal);
        }
    }

    if (sizeofLevels[l1 + 1] > maxSize[l1 + 1])
        compact(l1 + 1);
}

void Levels::SSTtoPq(std::shared_ptr<SSTable> s, std::priority_queue<heapNode, std::vector<heapNode>, std::greater<heapNode>> &entrypq, uint8_t lsmlevel)
{
    std::ifstream infile(s->filename, std::ios::binary);

    infile.seekg(s->footerOffset, std::ios::beg);

    uint64_t bloomOffset = 0;
    uint64_t sparseIndexOffset = 0;
    uint8_t strlength;
    uint64_t fileid = s->filecount;
    uint8_t tombstone;

    infile.read((char *)(&bloomOffset), sizeof(bloomOffset));
    infile.read((char *)(&sparseIndexOffset), sizeof(sparseIndexOffset));

    infile.seekg(0, std::ios::beg);

    while (infile && (uint64_t)infile.tellg() < bloomOffset)
    {
        if (!infile.read((char *)&strlength, sizeof(strlength)))
            break;
        std::string searchkey(strlength, '\0');
        if (!infile.read(searchkey.data(), (size_t)strlength))
            break;

        if (!infile.read((char *)&strlength, sizeof(strlength)))
            break;
        std::string value(strlength, '\0');
        if (!infile.read(value.data(), (size_t)strlength))
            break;

        if (!infile.read((char *)&tombstone, sizeof(uint8_t)))
            break;

        entrypq.push(heapNode(searchkey, value, tombstone, fileid, lsmlevel));
    }

    infile.close();
}

bool Levels::initialize()
{
    maxlevels = 5;
    maxSize.assign(maxlevels, 0);
    maxSize[0] = 2048 * 5;
    for (int i = 1; i < maxlevels; i++)
    {
        maxSize[i] = maxSize[i - 1] * 5;
    }
    sizeofLevels.assign(maxlevels, 0);
    levelsInfo.assign(maxlevels, std::vector<std::shared_ptr<SSTable>>());

    std::string filename = filecounter.setfilename(manifest);
    int filecount = filecounter.setcount();
    manifeststream.open(filename, std::ios::binary);
    return manifeststream.is_open();
}

void Levels::addSST(std::shared_ptr<SSTable> s)
{
    levelsInfo[0].push_back(s);
    sizeofLevels[0] += s->getfilesize();
    if (sizeofLevels[0] > maxSize[0])
    {
        // std::cout << "yes" << std::endl;
        compact(0);
    }
}

std::string Levels::searchLevels(std::string key)
{
    for (const auto &l : levelsInfo)
    {
        if (l.size() == 0)
            return "";
        for (std::shared_ptr<SSTable> s : l)
        {
            if (!(key < s->startkey || key > s->endkey))
            {
                std::string searchedkey = s->searchKey(key);
                if (searchedkey.size() > 0)
                    return searchedkey;
            }
        }
    }
    return "";
}