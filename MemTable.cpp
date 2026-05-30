#include "memtable.h"

float getZeroToOne()
{
    static std::mt19937 engine(std::random_device{}());
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(engine);
}

Entry::Entry(std::string key, std::string value)
    : key(key), value(value), tombStone(false)
{
}

Entry::Entry(std::string key, std::string value, int tomb)
    : key(key), value(value), tombStone(static_cast<bool>(tomb))
{
}

MemTable::MemTable()
{
    maxlevel = 5;

    head = new Element;
    head->entry = std::make_unique<Entry>("", "", 0);
    head->next.reserve(maxlevel);

    for (int i = 0; i <= maxlevel; i++)
        head->next.push_back(nullptr);
    size += sizeof(Element);
    size += head->next.capacity() * sizeof(Element *);
    size += sizeof(Entry);
    level = 0;
    p = 0.5f;
    if (!isFileEmpty(wal))
    {
        WALtoMem();
    }
    WALinitialize();
}

MemTable::~MemTable()
{
    Element *curr = head;
    Element *n = head->next[0];
    while (curr != nullptr)
    {
        delete curr;
        curr = n;
        if (n != nullptr)
            n = n->next[0];
    }
}

void MemTable::insertElement(std::string key, std::string newvalue, int tombStone, int writetowal)
{
    std::vector<Element *> searchStack = searchTraversal(key);
    int ind = static_cast<int>(searchStack.size()) - 1;
    Element *prev = searchStack[ind];

    if (prev->entry->key == key)
    {
        size += newvalue.size() - prev->entry->value.size();
        prev->entry->value = newvalue;
        prev->entry->tombStone = tombStone;

        if (writetowal)
            writeWAL(key, newvalue, tombStone);
        return;
    }
    nkeys++;
    int currlevel = 0;
    Element *newElement = new Element;
    newElement->entry = std::make_unique<Entry>(key, newvalue, 0);

    newElement->next.push_back(prev->next[currlevel]);
    prev->next[currlevel] = newElement;

    float flip = getZeroToOne();
    while (flip > 0.5f && currlevel < maxlevel)
    {
        if (currlevel < level)
        {
            ind--;
            prev = searchStack[ind];
            currlevel++;
            newElement->next.push_back(prev->next[currlevel]);
            prev->next[currlevel] = newElement;
        }
        else
        {
            currlevel++;
            newElement->next.push_back(nullptr);
            head->next[currlevel] = newElement;
        }
        flip = getZeroToOne();
    }
    size += sizeof(Element);
    size += newElement->next.capacity() * sizeof(Element *);
    size += sizeof(Entry) + key.size() + newvalue.size();

    level = std::max(level, currlevel);

    if (writetowal)
        writeWAL(key, newvalue, tombStone);
}

std::vector<Element *> MemTable::searchTraversal(std::string key)
{
    std::vector<Element *> searchStack;
    Element *curr = head;
    int currlevel = level;
    searchStack.push_back(curr);

    while (currlevel >= 0)
    {
        while (curr->next[currlevel] != nullptr &&
               curr->next[currlevel]->entry->key <= key)
        {
            curr = curr->next[currlevel];
        }

        if (curr->entry->key == key)
        {
            searchStack.push_back(curr);
            return searchStack;
        }

        currlevel--;
        searchStack.push_back(curr);
    }

    return searchStack;
}

void MemTable::deleteElement(std::string key)
{
    insertElement(key, "", 1);
}

std::string MemTable::getElement(std::string key)
{

    Element *curr = head;
    int currlevel = level;

    while (currlevel >= 0)
    {
        while (curr->next[currlevel] != nullptr &&
               curr->next[currlevel]->entry->key <= key)
        {
            curr = curr->next[currlevel];
        }

        if (curr->entry->key == key)
        {
            return curr->entry->value;
        }

        currlevel--;
    }
    return "";
}
bool MemTable::writeWAL(std::string key, std::string value, uint8_t tombstone)
{
    if (!walstream)
        return 0;

    uint8_t keylength = (uint8_t)key.size();
    uint8_t vallength = (uint8_t)value.size();

    walstream.write((char *)&keylength, sizeof(keylength));
    walstream.write(key.data(), keylength);

    walstream.write((char *)&vallength, sizeof(vallength));
    walstream.write(value.data(), vallength);

    walstream.write((char *)&tombstone, sizeof(tombstone));

    walstream.flush();

    return !walstream.bad();
}
void MemTable::WALtoMem()
{
    std::ifstream infile(wal, std::ios::binary);
    uint8_t keylength;
    uint8_t vallength;
    uint8_t tombstone;

    while (!infile.eof())
    {

        if (!infile.read((char *)&keylength, sizeof(keylength)))
            break;
        std::string key(keylength, '\0');
        if (!infile.read(key.data(), keylength))
            break;

        if (!infile.read((char *)&vallength, sizeof(vallength)))
            break;
        std::string value(vallength, '\0');
        if (!infile.read(value.data(), vallength))
            break;

        if (!infile.read((char *)&tombstone, sizeof(tombstone)))
            break;

        insertElement(key, value, tombstone, 0);
    }
    infile.close();
}

bool MemTable::isFileEmpty(const std::string &filename)
{
    if (!std::filesystem::exists(filename))
    {
        return true;
    }

    return std::filesystem::file_size(filename) == 0;
}

bool MemTable::WALinitialize()
{
    wal = filecounter.setfilename("wal");
    uint64_t filecount = filecounter.setcount();
    walstream.open(wal, std::ios::binary | std::ios::app);
    return walstream.is_open();
}

bool MemTable::isEmpty(){
    return nkeys==0;
}