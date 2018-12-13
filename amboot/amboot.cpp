#include <fstream>
#include <iostream>
#include <list>
#include <string.h>

using namespace std;

const streampos RESERVED_SIZE = 32768; // Size of MBR+partition list 
const int BYTES_TO_GIB = 30; // Gibi = 2**30 https://en.wikipedia.org/wiki/Binary_prefix
const int SECTOR_SIZE = 512;

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
    err::status statusError;
    int count;
    streampos size; // size of device in bytes
    string name;
    ofstream device;
};

ImageWriter::ImageWriter(const char *deviceName):
    statusError(err::ok),
    count(0),
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
    if (count == 0)
    {
        
    }

    count++; // increment count only if success
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
    static list<ImageReader *> factory(const char *listFileName)
    {
        ifstream listFile(listFileName);
        err::status statusError = err::ok;
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
                    cerr << "Error: listfile " << listFileName << " line " << lineNumber << " exceeds " << sizeof(line)-1 << " characters." << endl;
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
                if (statusError = reader->error())
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
            clear(imageReaders);
            imageReaders.clear();
        }

        return imageReaders;
    }
    static void clear(list<ImageReader *> &imageReaders)
    {
        for (auto reader : imageReaders)
        {
            delete reader;
        }
    }
private:
    err::status error() const
    {
        return statusError;
    }
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

err::status performBuild(const char *device, const char *list)
{
    ImageWriter w(device);
    if (w.error())
    {
        return w.error();
    }

    err::status statusError = err::ok;

    auto imageReaders = ImageReader::factory(list);

    { // Check there is enough space on dst drive for all extended images and MBS
        int requiredGiB = 0;
        for (auto reader : imageReaders)
        {
            requiredGiB += reader->getSizeGiB();
            if (requiredGiB > ((w.getSize() - RESERVED_SIZE) >> BYTES_TO_GIB))
            {
                statusError = err::space;
                cerr << "Error: not enough space. Dst available:" << ((w.getSize() - RESERVED_SIZE) >> BYTES_TO_GIB) << "GiB. Required:" << requiredGiB << "GiB." << endl ;
                ImageReader::clear(imageReaders);
                imageReaders.clear();
            }
        }
    }

    for (auto reader: imageReaders)
    {
        statusError = w.write(reader->getStream(), reader->getName(), reader->getSizeGiB());
        if (statusError)
            break;
    }
    ImageReader::clear(imageReaders);
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
        return 254;
    }
    err::status returnStatus = 0;
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

