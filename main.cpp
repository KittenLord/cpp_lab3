#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>
#include <fstream>
#include <cstring>

#define BUFFER_SIZE 1024
#define TOP_LETTERS 10
#define DO_WAITING true
#define THREAD_COUNT 3
#define DO_LOGGING false




#if DO_LOGGING
    #define log(id, args) do { LogMutex.lock(); std::cout << "[" << id << "] " << args << std::endl; LogMutex.unlock(); } while(0);
#else
    #define log(id, args)
#endif

typedef struct {
    int version;
    bool error;

    char *buffer;
    size_t totalLength;
    size_t availableLength;
} FileData;

typedef struct {
    int version;
    bool error;

    int lineCount;
    int wordCount;
    size_t availableCount;
    size_t totalCount;
    int byteFrequency[256];
} FileStats;

std::mutex FileReadingLock;
FileData fileData;

std::mutex FileStatsLock;
FileStats fileStats;

std::mutex LogMutex;

void receiveData(void *data) {
    std::thread::id id = std::this_thread::get_id();
    log(id, "Started");

    std::cout << "Which file would you like to analyze?" << std::endl;

    std::string filePath;
    std::cin >> filePath;
    log(id, "Received file path");

    std::ifstream file(filePath, std::ios_base::ate | std::ifstream::binary);

    if(file.fail()) {
        std::cout << "File could not be opened" << std::endl;
        fileData = (FileData){
            .error = true,
        };
        return;
    }
    
    size_t length = file.tellg();
    log(id, "Total file length:" << length);

    fileData = (FileData){
        .version = 1,
        .error = false,
        .buffer = (char *)malloc(length),
        .totalLength = length,
        .availableLength = 0,
    };

    file.seekg(0, std::ios_base::seekdir::_S_beg);

    while(fileData.availableLength < fileData.totalLength) {
        char buffer[BUFFER_SIZE];
        // NOTE: advances file position
        size_t bytesRead = file.readsome(buffer, BUFFER_SIZE);

        // NOTE: reading files is very expensive...
        if(DO_WAITING) std::this_thread::sleep_for(std::chrono::seconds(1));
        log(id, "Read " << bytesRead << " bytes");

        FileReadingLock.lock();

            std::memcpy(fileData.buffer + fileData.availableLength, buffer, bytesRead);
            fileData.availableLength += bytesRead;
            fileData.version += 1;

            // NOTE: copying data is incredibly expensive...
            if(DO_WAITING) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log(id, "Copied file data");

        FileReadingLock.unlock();
    }

    log(id, "Finished");
    return;
}

void calculateStats(void *data) {
    std::thread::id id = std::this_thread::get_id();
    log(id, "Started");

    size_t lastAvailableLength = 0;
    size_t lastVersion = 0;

    bool lastCharacterWasWhitespace = false;

    while(true) {
        FileReadingLock.lock();

        if(fileData.error) {
            FileReadingLock.unlock();
            FileStatsLock.lock();

            fileStats.error = true;

            FileStatsLock.unlock();
            break;
        }

        if(lastVersion != fileData.version) {
            lastVersion = fileData.version;

            int extraLines = 0;
            int extraWords = 0;

            int extraFrequency[256] = {0};

            for(int i = lastAvailableLength; i < fileData.availableLength; i++) {
                char c = fileData.buffer[i];
                extraFrequency[c] += 1;

                if(c != '\n' && c != ' ') lastCharacterWasWhitespace = false;

                if(c == '\n') {
                    extraLines += 1;
                }

                if((c == ' ' || c == '\n') && !lastCharacterWasWhitespace) {
                    extraWords += 1;
                    lastCharacterWasWhitespace = true;
                }
            }

            lastAvailableLength = fileData.availableLength;

            // NOTE: analyzing data is insanely expensive
            if(DO_WAITING) std::this_thread::sleep_for(std::chrono::seconds(1));
            log(id, "Calculated stats");

            FileReadingLock.unlock();

            FileStatsLock.lock();

                fileStats.lineCount += extraLines;
                fileStats.wordCount += extraWords;
                fileStats.availableCount = fileData.availableLength;
                fileStats.totalCount = fileData.totalLength;
                fileStats.version += 1;

                for(int i = 0; i < 256; i++) {
                    fileStats.byteFrequency[i] += extraFrequency[i];
                }

                // NOTE: copying data is indescribably expensive
                if(DO_WAITING) std::this_thread::sleep_for(std::chrono::milliseconds(500));
                log(id, "Copied stats");

            FileStatsLock.unlock();
        }
        else {
            FileReadingLock.unlock();
        }

        if(fileStats.availableCount == fileStats.totalCount && lastVersion != 0) break;
    }

    log(id, "Finished");
    return;
}

void displayStats(void *data) {
    std::thread::id id = std::this_thread::get_id();
    log(id, "Started");

    int lastVersion = 0;

    while(true) {
        bool shouldBreak = false;

        FileStatsLock.lock();

        if(fileStats.error) {
            FileStatsLock.unlock();
            break;
        }

        if(lastVersion != fileStats.version) {
            lastVersion = fileStats.version;
            shouldBreak = fileStats.availableCount == fileStats.totalCount && fileStats.version != 0;

            std::cout << "Reading progress: [" << fileStats.availableCount << "/" << fileStats.totalCount << "]" << std::endl;
            std::cout << "Total words: " << fileStats.wordCount << std::endl;
            std::cout << "Total lines: " << fileStats.lineCount << std::endl;

            char topLetters[TOP_LETTERS] = {0};
            int topFrequencies[TOP_LETTERS] = {0};
            int topLoaded = 0;

            for(int top = 0; top < TOP_LETTERS; top++) {
                for(int i = 0; i < 256; i++) {
                    if(i == ' ' || i == '\n') continue;

                    bool alreadyAccounted = false;
                    for(int top = 0; top < topLoaded; top++) {
                        if(topLetters[top] == i) alreadyAccounted = true;
                    }
                    if(alreadyAccounted) continue;

                    if(topFrequencies[top] < fileStats.byteFrequency[i]) {
                        topFrequencies[top] = fileStats.byteFrequency[i];
                        topLetters[top] = i;
                    }
                }

                topLoaded += 1;
            }

            std::cout << "Top letters: " << std::endl;
            for(int i = 0; i < TOP_LETTERS; i++) {
                std::cout << "   '" << topLetters[i] << "' - " << topFrequencies[i] << std::endl;
            }

            // NOTE: displaying data is scarily expensive
            if(DO_WAITING) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log(id, "Displayed stats");
        }


        FileStatsLock.unlock();
        
        if(shouldBreak) break;
    }

    log(id, "Finished");
    return;
}

void noop(void *) {
    std::thread::id id = std::this_thread::get_id();
    log(id, "Started");

    log(id, "Finished");
    return;
}

int main() {
    fileData = {0};
    fileStats = {0};

    std::thread threads[THREAD_COUNT];

    for(int i = 0; i < THREAD_COUNT; i++) {
        if(false) {}
        else if(i == 0) {
            threads[i] = std::thread(receiveData, nullptr);
        }
        else if(i == 1) {
            threads[i] = std::thread(calculateStats, nullptr);
        }
        else if(i == 2) {
            threads[i] = std::thread(displayStats, nullptr);
        }
        else {
            threads[i] = std::thread(noop, nullptr);
        }
    }

    for(int i = 0; i < THREAD_COUNT; i++) {
        threads[i].join();
    }

    return 0;
}
