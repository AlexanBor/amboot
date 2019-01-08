#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <string.h>
#include <unistd.h>

using namespace std;

constexpr unsigned int HEADER_SIZE = 32768; // Size of MBR+partition list 
constexpr unsigned int BYTES_TO_GIB = 30;   // Gibi = 2**30 https://en.wikipedia.org/wiki/Binary_prefix
constexpr unsigned int SECTOR_SIZE = 512;
constexpr unsigned int BYTES_TO_SECTORS = 9;//  SECTOR_SIZE = 2**9
constexpr unsigned int BUFFER_SIZE = 1024 * 1024; // 1 Mibi byte
constexpr unsigned int SECTORS_PER_GiB = 1024 * 1024 * 1024 / SECTOR_SIZE;
constexpr uint8_t MAGIC_XBR = 0x42;
constexpr uint16_t MAGIC_MBR = (uint16_t)0xAA55;

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
struct ExtBootRecord // Used only to store SECTOR_SIZE of MAGIC_XBR bytes
{
    char code[440];
    uint32_t disk_signature;
    uint16_t unused;
    PartitionMBR partition[4];
    uint16_t mbr_signature;
};
struct ImageInfo
{
    uint32_t firstSectorLBA;
    uint32_t sectorsCountLBA;
    uint32_t part0firstSectorLBA;
    uint32_t reserved_;
    char imageName[SECTOR_SIZE - 4 * sizeof(uint32_t)];
};
constexpr size_t MAX_IMAGECOUNT = (HEADER_SIZE - sizeof(ExtBootRecord) - sizeof(ExtBootRecord)) / sizeof(ImageInfo);
struct DiskHeader
{
    MasterBootRecord mbr;
    ExtBootRecord xbr;
    ImageInfo images[MAX_IMAGECOUNT];
};
// check sizes and alignments at compile time:
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

void fillImageName(char *dst, const char *src, size_t size)
{
    const char *lastPos = strrchr(src, '/');
    if (lastPos)
    {
        src = lastPos+1;
    }
    strncpy(dst, src, size - 1);
    dst[size - 1] = 0;
}

unsigned getSize(const char *sizeStr)
{
    char *endptr;
    unsigned long size = strtoul(sizeStr, &endptr, 10);
    if (size < 1 || size > __UINT32_MAX__ || *endptr != 0)
    {
        return 0;
    }
    return (unsigned)size;
}
namespace err
{
    enum status
    {
        ok = 0,     // Everything is OK, no error
        byteorder,  // Compile and run only on little-endian byteorder!
        cmdLine,    // Wrong command line
        dstOpen,    // Error opening dst
        dstSeek,    // Error getting dst size
        dstFail,    // Fail writing dst device
        dstRead,    // Error reading dst device
        dstFlush,   // Error flushing dst device
        listOpen,   // Error opening images list
        listLimit,  // Line length in images list ecxeeds limit
        listLine,   // Cannot parse line in image list
        srcOpen,    // Error opening one of src images
        emptyList,  // Error: no image files defined in list file
        imageToBig, // Image file doesn't fit in requested size
        increase,   // Error: increase size for partition # name
        imageCount, // Error: MAX_IMAGECOUNT exceeded      
        noActive,   // Error: no active partition selected.
        noMagic,    // ExtBootRecord xbr contains no expected magic
        emptyBoot,  // Error: No images found on device
        mbrMagic,   // Error: no magic in mbr of image
        imageNum,   // Error: image number is greater then count of images
        space       // Not enough space on dst device
    };
};

class ImageKeeper
{
public:
    ImageKeeper(const char *deviceName, bool preview_);
    err::status error() const
    {
        return statusError;
    }
    err::status write(istream &image, const string &imageName, int imageSizeGiB);
    err::status saveBoot(unsigned bootNumber);
    err::status readBoot();
    err::status print();
    streampos getSize() const
    {
        return size;
    }
private:
    void write(const char *buffer, streamsize size)
    {
        if (preview)
        {
            return;
        }
        device.write(buffer, size);
        if (device.fail())
        {
            statusError = err::dstFail;
            cerr << "Error: fail wrining image to " << name << "." << endl;
        }
    }
    void read(char *buffer, streamsize size)
    {
        device.read(buffer, size);
        if (device.fail())
        {
            cerr << "Error reading dst device " << name << endl;
            statusError = err::dstRead;
        }
    }
    void seek(ios::streamoff offset, ios_base::seekdir dir)
    {
        device.seekp(offset, dir);
        if (device.fail())
        {
            cerr << "Error seeking dst device " << name << endl;
            statusError = err::dstSeek;
        }
    }
    err::status statusError;
    unsigned imagesCount;
    bool preview;
    streampos size; // size of device in bytes
    string name;
    fstream device;
    DiskHeader hdr;
};
ImageKeeper::ImageKeeper(const char *deviceName, bool preview_):
    statusError(err::ok),
    imagesCount(0),
    preview(preview_),
    size(0),
    name(deviceName),
    device(deviceName, (preview_ ? ios::in : (ios::out | ios::in)) | ios::binary)
{
    memset(&hdr, 0, sizeof(hdr));
    if (device.eof() || device.bad() || !device.is_open())
    {
        cerr << "Error opening dst device " << name << endl;
        statusError = err::dstOpen;
    }
    else
    {
        seek(0, ios::end);
        if (statusError)
        {
            return;
        }
        size = device.tellp();
        seek(HEADER_SIZE, ios::beg);
        if (statusError)
        {
            return;
        }
    }
    if (statusError)
    {
        device.close();
    }
}
err::status ImageKeeper::write(istream &image, const string &imageName, int imageSizeGiB)
{
    unique_ptr<char> buffer(new char[BUFFER_SIZE]);
    streamsize imageSizeBytes = streamsize(imageSizeGiB) << BYTES_TO_GIB; // Size in bytes of current image/partition
    streamsize totalCount = 0;

    hdr.images[imagesCount].firstSectorLBA = imagesCount ? hdr.images[imagesCount-1].firstSectorLBA + hdr.images[imagesCount-1].sectorsCountLBA : HEADER_SIZE >> BYTES_TO_SECTORS;
    hdr.images[imagesCount].sectorsCountLBA = imageSizeGiB << (BYTES_TO_GIB - BYTES_TO_SECTORS);
    fillImageName(hdr.images[imagesCount].imageName, imageName.c_str(), sizeof(hdr.images[imagesCount].imageName));

    bool mbrCalculated = false;

    bool showProgress = isatty(1); // 0=stdin 1=stdout 2=stderr

    cout << "Info: writing " << imageName << endl << imageSizeBytes << " bytes total." << endl;

    while (image.read(buffer.get(), BUFFER_SIZE))
    {
        streamsize countRead = image.gcount();

        if (!mbrCalculated)
        {
            MasterBootRecord &mbr = *(MasterBootRecord *)buffer.get();
            auto newSectorsCountLBA = (imageSizeBytes >> BYTES_TO_SECTORS) - mbr.partition[1].firstSectorLBA;
            if (mbr.partition[1].sectorsCountLBA > newSectorsCountLBA)
            {
                int requiredGiB = (mbr.partition[1].sectorsCountLBA + mbr.partition[1].firstSectorLBA - 1 + SECTORS_PER_GiB) / SECTORS_PER_GiB;
                cerr << "Error: size of image " << imageName << " #" << imagesCount+1 << " requires at least " << requiredGiB << "GiB" << endl;
                statusError = err::increase;
                return statusError;
            }
            hdr.images[imagesCount].part0firstSectorLBA = mbr.partition[0].firstSectorLBA;
            mbr.partition[1].sectorsCountLBA = (uint32_t)newSectorsCountLBA;
            mbrCalculated = true;
        }

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

        if (showProgress)
        {
            cout << totalCount << '\r';
            cout.flush();
        }
        if (countRead < BUFFER_SIZE)
        {
            break;
        }
    }

    memset(buffer.get(), 0, BUFFER_SIZE);
    while (totalCount < imageSizeBytes - BUFFER_SIZE)
    {
        write(buffer.get(), BUFFER_SIZE);
        totalCount += BUFFER_SIZE;
        if (statusError)
        {
            return statusError;
        }
        if (showProgress)
        {
            cout << totalCount << '\r';
            cout.flush();
        }
    }
    write(buffer.get(), imageSizeBytes - totalCount);
    if (statusError)
    {
        return statusError;
    }
    if (showProgress)
    {
        cout << '\r' << "Write completed." << endl;
        cout.flush();
    }

    imagesCount++; // increment count only if success
    return statusError;
}
err::status ImageKeeper::print()
{
    int activeNumber = 0;
    for (unsigned i = 0; i < imagesCount; i++)
    {
        char isActive = ' ';
        if (hdr.mbr.partition[0].firstSectorLBA == hdr.images[i].firstSectorLBA + hdr.images[i].part0firstSectorLBA)
        {
            isActive = '*';
            activeNumber = i + 1;
        }
        cout << isActive << ' ' << i+1 << ": " << hdr.images[i].imageName << endl;
    }
    if (hdr.mbr.partition[0].firstSectorLBA == 0 && hdr.mbr.mbr_signature == 0) // MBR is zeroed
    {
        cout << "MBR is zeroed" << endl;
    }
    else if (activeNumber)
    {
        cout << "* - active partition " << activeNumber << endl;
    }
    else
    {
        cout << "First MBR partition points outside." << endl;
        cerr << "Error: no active partition selected in MBR." << endl;
        statusError = err::noActive;
    }
    return statusError;
}
err::status ImageKeeper::saveBoot(unsigned bootNumber)
{
    memset(&hdr.xbr, MAGIC_XBR, sizeof(hdr.xbr));
    if (bootNumber > imagesCount)
    {
        cerr << "Error: image number is greater then count of images on device " << name << endl;
        return (statusError = err::imageNum);
    }
    bootNumber--;
    seek(hdr.images[bootNumber].firstSectorLBA << BYTES_TO_SECTORS, ios::beg);
    if (statusError)
    {
        return statusError;
    }
    if (preview)
    {
        return statusError;
    }
    read((char *)&hdr.mbr, sizeof(hdr.mbr));
    if (statusError)
    {
        return statusError;
    }
    if (hdr.mbr.mbr_signature != MAGIC_MBR)
    {
        cerr << "Error: no magic in mbr of image " << bootNumber+1 << ' ' << hdr.images[bootNumber].imageName << endl;
        return (statusError = err::mbrMagic);
    }

    hdr.mbr.partition[0].firstSectorLBA += hdr.images[bootNumber].firstSectorLBA;
    hdr.mbr.partition[1].firstSectorLBA += hdr.images[bootNumber].firstSectorLBA;
    hdr.mbr.partition[2].firstSectorLBA = (HEADER_SIZE >> BYTES_TO_SECTORS);
    hdr.mbr.partition[2].sectorsCountLBA = hdr.images[imagesCount-1].sectorsCountLBA + hdr.images[imagesCount-1].firstSectorLBA - (HEADER_SIZE >> BYTES_TO_SECTORS);
    hdr.mbr.partition[2].partition_type = 0x1F;

    seek(0, ios::beg);
    if (statusError)
    {
        cerr << "Error seeking dst device " << name << endl;
        return (statusError = err::dstSeek);
    }
    write((char *)&hdr, sizeof(hdr));
    if (!statusError)
    {
        device.flush();
    }
    if (device.fail())
    {
        cerr << "Error flushing dst device " << name << endl;
        return (statusError = err::dstFlush);
    }
    return statusError;
}
err::status ImageKeeper::readBoot()
{
    seek(0, ios::beg);
    if (statusError)
    {
        return statusError;
    }
    read((char *)&hdr, sizeof(hdr));
    if (statusError)
    {
        return statusError;
    }
    
    for (size_t i = 0; i < sizeof(hdr.xbr); i++)
        if (((char *)&hdr.xbr)[i] != MAGIC_XBR)
        {
            cerr << "Error: ExtBootRecord xbr contains no expected magic." << endl;
            return (statusError = err::noMagic);
        }

    for (imagesCount = 0; imagesCount < MAX_IMAGECOUNT; imagesCount++)
    {
        if (hdr.images[imagesCount].firstSectorLBA == 0) // looks like this image is zeroed ie empty
            break;
    }

    if (imagesCount <= 0)
    {
        cerr << "Error: No images found on device " << name << endl;
        statusError = err::emptyBoot;
        return statusError;
    }

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
    list<unique_ptr<ImageReader>> &items()
    {
        return images;
    }
    void clear()
    {
        images.clear();
    }
    err::status error() const
    {
        return statusError;
    }
private:
    list<unique_ptr<ImageReader>> images;
    err::status statusError;
};
ImageList::ImageList(const char *listFileName):
    statusError(err::ok)
{
    ifstream listFile(listFileName, ios::in | ios::binary);

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
                if (!listFile.eof())
                {
                    cerr << "Error: listfile " << listFileName << " line " << lineNumber << " exceeds " << sizeof(line) - 1 << " characters." << endl;
                    statusError = err::listLimit;
                }
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
            unsigned size = getSize(toks);
            if (size == 0)
            {
                cerr << "Error: listfile " << listFileName << " line " << lineNumber << ". Incorrect size." << endl;
                statusError = err::listLine;
                break;
            }
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
#if 0 // #ifndef NDEBUG
            cout << "Info:" << lineNumber << ":" << size << " <<" << name << ">>" << endl;
#endif
            unique_ptr<ImageReader> reader(new ImageReader(size, name));
            if ((statusError = reader->error()))
            {
                // delete reader;
                break;
            }
            images.push_back(move(reader));
        }
        if (images.size() == 0)
        {
            cerr << "Error: no image files defined in " << listFileName << endl;
            statusError = err::emptyList;
        }
        else if (images.size() > MAX_IMAGECOUNT)
        {
            cerr << "Error: image count is " << images.size() << " in " << listFileName << ". Limit is " << MAX_IMAGECOUNT << endl;
            statusError = err::imageCount;
        }
    }

    if (statusError)
    {
        clear();
    }
}

err::status performBuild(const char *dstDevice, const char *listFileName, bool preview, unsigned bootNumber)
{
    err::status statusError = err::ok;

    ImageList imageList(listFileName);
    if (imageList.error())
    {
        return imageList.error();
    }

    if (bootNumber > imageList.items().size())
    {
        cerr << "Error: image number is greater then count of images on device " << dstDevice << endl;
        return err::imageNum;
    }

    ImageKeeper w(dstDevice, preview);
    if (w.error())
    {
        return w.error();
    }

    { // Check there is enough space on dst drive for all extended images and MBS
        int requiredGiB = 0;
        for (auto &reader : imageList.items())
        {
            requiredGiB += reader->getSizeGiB();
        }
        if (streampos(requiredGiB) > ((w.getSize() - streampos(HEADER_SIZE)) >> BYTES_TO_GIB))
        {
            cerr << "Error: not enough space. Dst available:" << ((w.getSize() - streampos(HEADER_SIZE)) >> BYTES_TO_GIB) << "GiB. Required:" << requiredGiB << "GiB." << endl;
            imageList.clear();
            return err::space;
        }
    }

    for (auto &reader: imageList.items())
    {
        statusError = w.write(reader->getStream(), reader->getName(), reader->getSizeGiB());
        if (statusError)
            break;
    }
    if (!statusError)
    {
        statusError = w.saveBoot(bootNumber);
    }
    return statusError;
}

err::status performList(const char *device)
{
    ImageKeeper w(device, true); // Simulate preview mode to avoid disk write

    if (w.error() ||
        w.readBoot())
    {
        return w.error();
    }

    return w.print();
}

err::status performSwitch(const char *device, unsigned bootNumber)
{
    ImageKeeper w(device, false);

    if (w.error() ||
        w.readBoot())
    {
        return w.error();
    }

    return w.saveBoot(bootNumber);
}

void printUsage()
{
    cerr << "Usage is:\n"
            "amboot b /dev/sd? /full/path/to/imagelistfile [bootNumber]\n"
            "\tbuild on specified device and set boot image to bootNumber, 1 to " << MAX_IMAGECOUNT << "\n"
            "amboot p /dev/sd? /full/path/to/imagelistfile [bootNumber]\n"
            "\tpreview: simulate b_uild without actually write to device\n"
            "amboot l /dev/sd?\n"
            "\tlist image chain on specified device\n"
            "amboot s /dev/sd? bootNumber\n"
            "\tset boot image to bootNumber, 1 to " << MAX_IMAGECOUNT << " on specified device /dev/sd? (/dev/sda...)\n"
         << endl;
}

bool isLittleEndian()
{
    int num   = 0x1;
    char *ptr = (char *)&num;
    return (*ptr == 1);
}

unsigned getBootNumber(const char *num)
{
    char *endptr;
    unsigned long bootNumber = strtoul(num, &endptr, 10);
    if (bootNumber < 1 || bootNumber > MAX_IMAGECOUNT || *endptr != '\0')
    {
        printUsage();
        return 0;
    }
    return (unsigned int)bootNumber;
}

// p /dev/sdc /home/vagrant/amboot/list.txt
// l /dev/sdc
// s /dev/sdc 1 
int main(int argc, char **argv)
{
    if (!isLittleEndian())
    {
        cerr << "Fatal: compile and run only on little-endian byteorder!" << endl;
        return err::byteorder;
    }
    err::status returnStatus = err::ok;
    if (argc <= 1 || argv[1][1] != 0)
    {
        printUsage();
        return err::cmdLine;
    }
    unsigned bootNumber = 1;
    switch (argv[1][0])
    {
    case 'p':
    case 'b':
        if (argc == 5)
        {
            bootNumber = getBootNumber(argv[4]);
            if (bootNumber < 1)
            {
                return err::cmdLine;
            }
        }
        else if (argc != 4)
        {
            printUsage();
            return err::cmdLine;
        }
        returnStatus = performBuild(argv[2], argv[3], argv[1][0] == 'p' ? true : false, bootNumber);
        break;
    case 'l':
        if (argc != 3)
        {
            printUsage();
            return err::cmdLine;
        }
        returnStatus = performList(argv[2]);
        break;
    case 's':
        if (argc != 4)
        {
            printUsage();
            return err::cmdLine;
        }
        bootNumber = getBootNumber(argv[3]);
        if (bootNumber < 1)
        {
            return err::cmdLine;
        }
        returnStatus = performSwitch(argv[2], bootNumber);
        break;
    default:
        printUsage();
        return err::cmdLine;
    }

    return returnStatus;
}
