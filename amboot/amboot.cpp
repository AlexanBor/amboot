#include <fstream>
#include <iostream>
#include <list>
#include <string.h>
#include <memory>

using namespace std;

const int HEADER_SIZE = 32768; // Size of MBR+partition list 
const int BYTES_TO_GIB = 30; // Gibi = 2**30 https://en.wikipedia.org/wiki/Binary_prefix
const int SECTOR_SIZE = 512;
const int BUFFER_SIZE = 1024 * 1024; // 1 Mibi byte

#pragma pack(push, 1)
//{
struct CylHeadSec
{
    int8_t head;
    int16_t cyl_sec;
};
struct PartitionMBR
{
    uint8_t status;
    struct CylHeadSec first_sector;
    uint8_t partition_type;
    struct CylHeadSec last_sector;
    uint32_t firstSectorLBA;
    uint32_t sectorsCountLBA;
};
struct MasterBootRecord
{
    char code[440];
    uint32_t disk_signature;
    uint16_t unused;
    PartitionMBR partition[4];
    uint16_t mbr_signature;
};
struct ExtBootRecord
{
    char code[440];
    uint32_t disk_signature;
    uint16_t unused;
    PartitionMBR partition[4];
    uint16_t mbr_signature;
};
struct DiskHeader
{
    MasterBootRecord mbr;
    ExtBootRecord xbr;
    char images[32768-1024];
};
// check sizes and alignments at compile time
inline void check_struct()
{
    switch (0)
    {
    case 0:
        break;
    case ((sizeof(PartitionMBR) == 16) * 1):
        break;
    case ((sizeof(MasterBootRecord) == SECTOR_SIZE) * 2):
        break;
    case ((sizeof(DiskHeader) == HEADER_SIZE) * 3):
        break;
    }
}
//}
#pragma pack(pop)

namespace err
{
    enum status
    {
        ok = 0,     // Everything is OK, no error
        cmdLine,    // Wrong command line
        dstSeek,    // Error getting dst size
        dstOpen,    // Error opening dst
        srcOpen,    // Error opening one of src images
        listOpen,   // Error opening images list
        listLimit,  // Line length in images list ecxeeds limit
        listLine,   // Cannot prse line in image list
        emptyList,  // Error: no image files defined in list file
        imageToBig, // Image file doesn't fit in requested size
        dstFail,    // Fail writing dst device
        byteorder,  // Compile and run only on little-endian byteorder!
        space       // Not enough space on dst device
    };
};

class ImageWriter
{
public:
    ImageWriter(const char *deviceName);
    err::status error() const
    {
        return statusError;
    }
    err::status write(istream &image, const string &imageName, int imageSizeGiB);
    streampos getSize() const
    {
        return size;
    }
private:
    void write(const char *buffer, streamsize size)
    {
        device.write(buffer, size);
        if (device.fail())
        {
            statusError = err::dstFail;
            cerr << "Error: fail wrining image to " << name << "." << endl;
        }
    }

    err::status statusError;
    int imagesCount;
    streampos size; // size of device in bytes
    string name;
    ofstream device;
};

ImageWriter::ImageWriter(const char *deviceName):
    statusError(err::ok),
    imagesCount(0),
    size(0),
    name(deviceName),
    device(deviceName)
{
    if (device.eof() || device.bad() || !device.is_open())
    {
        cerr << "Error opening dst device " << name << endl;
        statusError = err::dstOpen;
    }
    else
    {
        size = device.seekp(0, ios::end).tellp();
        device.seekp(0, ios::beg);
        if (device.fail())
        {
            cerr << "Error seeking dst device " << name << endl;
            statusError = err::dstSeek;
        }
    }
    if (statusError)
    {
        device.close();
    }
}

err::status ImageWriter::write(istream &image, const string &imageName, int imageSizeGiB)
{
    unique_ptr<char> buffer(new char[BUFFER_SIZE]);
    streamsize imageSizeBytes = streamsize(imageSizeGiB) << BYTES_TO_GIB; // Size in bytes of dst image/partition
    streamsize totalCount = 0;

    while (image.read(buffer.get(), BUFFER_SIZE))
    {
        streamsize countRead = image.gcount();

        totalCount += countRead;

        if (totalCount > imageSizeBytes)
        {
            statusError = err::imageToBig;
            cerr << "Error: size of image " << imageName << " is greater than requested size " << imageSizeGiB << " GiB." << endl;
            return statusError;
        }

        write(buffer.get(), countRead);
        if (statusError)
        {
            return statusError;
        }

        if (countRead < BUFFER_SIZE)
        {
            break;
        }
    }

    memset(buffer.get(), 0, BUFFER_SIZE);
    while (totalCount < imageSizeBytes - BUFFER_SIZE)
    {
        device.write(buffer.get(), BUFFER_SIZE);
        if (statusError)
        {
            return statusError;
        }
    }
    write(buffer.get(), imageSizeBytes - totalCount);
    if (statusError)
    {
        return statusError;
    }
    
    imagesCount++; // increment count only if success
    return statusError;
}

class ImageReader
{
public:
    ImageReader(int size_, const char *fileName);
    istream &getStream()
    {
        return image;
    }
    const string &getName() const
    {
        return name;
    }
    int getSizeGiB() const
    {
        return size;
    }
    err::status error() const
    {
        return statusError;
    }
private:
    err::status statusError;
    int size;
    string name;
    ifstream image;
};

ImageReader::ImageReader(int size_, const char *fileName):
    statusError(err::ok),
    size(size_),
    name(fileName),
    image(fileName)
{
    if (image.eof() || image.bad() || !image.is_open())
    {
        cerr << "Error opening src image " << name << endl;
        statusError = err::srcOpen;
    }
    if (statusError)
    {
        image.close();
    }
}

class ImageList
{
public:
    ImageList(const char *listFileName);
    ~ImageList()
    {
        clear();
    }
    list <ImageReader *>items()
    {
        return images;
    }
    void clear()
    {
        for (auto image : images)
        {
            delete image;
        }
        images.clear();
    }
    err::status error() const
    {
        return statusError;
    }
private:
    list <ImageReader *>images;
    err::status statusError;
};

ImageList::ImageList(const char *listFileName):
    statusError(err::ok)
{
    ifstream listFile(listFileName, ios::in | ios::binary);
    list<ImageReader *> imageReaders;

    if (listFile.eof() || listFile.bad() || !listFile.is_open())
    {
        cerr << "Error opening images list " << listFileName << endl;
        listFile.close();
        statusError = err::listOpen;
    }
    else
    {
        char line[1024];
        int lineNumber = 0;
        while (!listFile.eof())
        {
            listFile.getline(line, sizeof(line));
            lineNumber++;
            if (listFile.fail())
            {
                cerr << "Error: listfile " << listFileName << " line " << lineNumber << " exceeds " << sizeof(line) - 1 << " characters." << endl;
                statusError = err::listLimit;
                break;
            }
            if (line[0] == 0 || line[0] == '#')
            {
                continue;
            }
            char *toks = strtok(line, " ");
            if (!toks)
            {
                cerr << "Error: listfile " << listFileName << " line " << lineNumber << " cannot parse line." << endl;
                statusError = err::listLine;
                break;
            }
            int size = atoi(toks);
            char *name = strtok(NULL, " ");
            if (!name)
            {
                cerr << "Error: listfile " << listFileName << " line " << lineNumber << " cannot parse line. Empty imageFileName?" << endl;
                statusError = err::listLine;
                break;
            }
            toks = strtok(NULL, " ");
            if (toks)
            {
                cerr << "Error: listfile " << listFileName << " line " << lineNumber << " cannot parse line. Space in imageFileName?" << endl;
                statusError = err::listLine;
                break;
            }
#ifndef NDEBUG
            cerr << "Info:" << lineNumber << ":" << size << " <<" << name << ">>" << endl;
#endif
            auto reader = new ImageReader(size, name);
            if ((statusError = reader->error()))
            {
                delete reader;
                break;
            }
            imageReaders.push_back(reader);
        }
        if (imageReaders.size() == 0)
        {
            cerr << "Error: no image files defined in " << listFileName << endl;
            statusError = err::emptyList;
        }
    }

    if (statusError)
    {
        clear();
    }
}

err::status performBuild(const char *dstDevice, const char *listFileName)
{
    ImageWriter w(dstDevice);
    if (w.error())
    {
        return w.error();
    }

    err::status statusError = err::ok;

    ImageList imageList(listFileName);

    { // Check there is enough space on dst drive for all extended images and MBS
        int requiredGiB = 0;
        for (auto reader : imageList.items())
        {
            requiredGiB += reader->getSizeGiB();
            if (streampos(requiredGiB) > ((w.getSize() - streampos(HEADER_SIZE)) >> BYTES_TO_GIB))
            {
                statusError = err::space;
                cerr << "Error: not enough space. Dst available:" << ((w.getSize() - streampos(HEADER_SIZE)) >> BYTES_TO_GIB) << "GiB. Required:" << requiredGiB << "GiB." << endl ;
                imageList.clear();
            }
        }
    }

    for (auto reader: imageList.items())
    {
        statusError = w.write(reader->getStream(), reader->getName(), reader->getSizeGiB());
        if (statusError)
            break;
    }
    return statusError;
}

err::status performList(const char *device)
{

    return err::ok;
}

err::status performSwitch(const char *device, const char *itemNum)
{

    return err::ok;
}

void printUsage()
{
    cerr << "Usage is:" << endl
        << "\tamboot b /dev/sd? /full/path/to/imagelistfile" << endl
        << "\t\tbuild on specified device" << endl
        << "\tamboot l /dev/sd?" << endl
        << "\t\tlist image chain on specified device" << endl
        << "\tamboot s /dev/sd? #" << endl
        << "\t\tswitch to target image # (1...) on specified device /dev/sd? (/dev/sda...)"
        << endl << endl;
}

bool isLittleEndian()
{
    int num   = 0x1;
    char *ptr = (char *)&num;
    return (*ptr == 1);
}

int main(int argc, char **argv)
{
    if (!isLittleEndian())
    {
        cerr << "Fatal: compile and run only on little-endian byteorder!" << endl;
        return err::byteorder;
    }
    err::status returnStatus = err::ok;
    if (argc <= 1)
    {
        printUsage();
        return err::cmdLine;
    }
    if (!strcmp(argv[1], "b"))
    {
        if (argc != 4)
        {
            printUsage();
            return err::cmdLine;
        }
        returnStatus = performBuild(argv[2], argv[3]);
    }
    else if (!strcmp(argv[1], "l"))
    {
        if (argc != 3)
        {
            printUsage();
            return err::cmdLine;
        }
        returnStatus = performList(argv[2]);
    }
    else if (!strcmp(argv[1], "s"))
    {
        if (argc != 4)
        {
            printUsage();
            return err::cmdLine;
        }
        returnStatus = performSwitch(argv[2], argv[3]);
    }
    else
    {
        printUsage();
        return err::cmdLine;
    }

    return returnStatus;
}

