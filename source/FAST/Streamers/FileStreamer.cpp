
#include "FAST/Exception.hpp"
#include "FileStreamer.hpp"
#include <fstream>
#include <chrono>
#include "FAST/Data/Image.hpp" // TODO should not be here

namespace fast {

FileStreamer::FileStreamer() {
    mStreamIsStarted = false;
    mNrOfReplays = 0;
    mIsModified = true;
    mLoop = false;
    mStartNumber = 0;
    mZeroFillDigits = 0;
    mFirstFrameIsInserted = false;
    mHasReachedEnd = false;
    mTimestampFilename = "";
    mNrOfFrames = -1;
    mSleepTime = 0;
    mStepSize = 1;
    mMaximumNrOfFrames = -1;
    mStop = false;
}

void FileStreamer::setNumberOfReplays(uint replays) {
    mNrOfReplays = replays;
}

int FileStreamer::getNrOfFrames() {
    if(mNrOfFrames == -1) {
        if(mMaximumNrOfFrames > 0) {
            mNrOfFrames = mMaximumNrOfFrames;
            return mNrOfFrames;
        }
        int frameCounter = 0;
        int frame = mStartNumber;
        int currentSequence = 0;
        while(true) {
            std::string filename = getFilename(frame, currentSequence);
            if(!fileExists(filename)) {
                // If file doesn't exists, move on to next sequence or stop
                if(currentSequence < mFilenameFormats.size()-1) {
                    currentSequence++;
                } else {
                    break;
                }
            }
            frame += mStepSize;
            frameCounter++;
        }
        mNrOfFrames = frameCounter;
    }
    return mNrOfFrames;
}

void FileStreamer::setSleepTime(uint milliseconds) {
    mSleepTime = milliseconds;
}

void FileStreamer::setMaximumNumberOfFrames(uint nrOfFrames) {
    mMaximumNrOfFrames = nrOfFrames;
}

void FileStreamer::setTimestampFilename(std::string filepath) {
    mTimestampFilename = filepath;
}

void FileStreamer::execute() {
    if(mFilenameFormats.size() == 0)
        throw Exception("No filename format was given to the FileStreamer");
    if(!mStreamIsStarted) {
        mStreamIsStarted = true;
        mThread = new std::thread(std::bind(&FileStreamer::producerStream, this));
    }

    // Wait here for first frame
    std::unique_lock<std::mutex> lock(mFirstFrameMutex);
    while(!mFirstFrameIsInserted) {
        mFirstFrameCondition.wait(lock);
    }
}

void FileStreamer::setFilenameFormat(std::string str) {
    if(str.find("#") == std::string::npos)
        throw Exception("Filename format must include a hash tag # which will be replaced by a integer starting from 0.");

    mFilenameFormats.clear();
    mFilenameFormats.push_back(str);
}

void FileStreamer::setFilenameFormats(std::vector<std::string> strs) {
    for(std::string str : strs) {
        if(str.find("#") == std::string::npos)
            throw Exception("Filename format must include a hash tag # which will be replaced by a integer starting from 0.");
    }
    mFilenameFormats = strs;
}

void FileStreamer::producerStream() {
    //Streamer::pointer pointerToSelf = mPtr.lock(); // try to avoid this object from being destroyed until this function is finished

    // Read timestamp file if available
    std::ifstream timestampFile;
    uint64_t previousTimestamp = 0;
    auto previousTimestampTime = std::chrono::high_resolution_clock::time_point::min();
    if(!mTimestampFilename.empty() && mUseTimestamp) {
        timestampFile.open(mTimestampFilename.c_str());
        if(!timestampFile.is_open()) {
            throw Exception("Timestamp file not found in FileStreamer");
        }

        // Fast forward to start
        if(mStartNumber > 0) {
            int i = 0;
            while(i <= mStartNumber) {
                std::string line;
                std::getline(timestampFile, line);
                ++i;
            }
        }
    }

    uint i = mStartNumber;
    int replays = 0;
    int currentSequence = 0;
    while(true) {
        {
            std::unique_lock<std::mutex> lock(mStopMutex);
            if(mStop) {
                mStreamIsStarted = false;
                mFirstFrameIsInserted = false;
                mHasReachedEnd = false;
                break;
            }
        }
        std::string filename = getFilename(i, currentSequence);
        try {
            reportInfo() << "Filestreamer reading " << filename << reportEnd();
            DataObject::pointer dataFrame = getDataFrame(filename);
            // Set and use timestamp if available
            if(!mTimestampFilename.empty() && mUseTimestamp) {
                std::string line;
                std::getline(timestampFile, line);
                if(!line.empty()) {
					uint64_t timestamp = std::stoull(line);
                    dataFrame->setCreationTimestamp(timestamp);
                }
            }

            if(dataFrame->getCreationTimestamp() != 0 && mUseTimestamp) {
                uint64_t timestamp = dataFrame->getCreationTimestamp();
                // Wait as long as necessary before adding image
                // Time passed since last frame
                auto timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - previousTimestampTime);
                while (timestamp > previousTimestamp + timePassed.count()) {
                    // Wait
                    int64_t left = (timestamp - previousTimestamp) - timePassed.count();
                    reportInfo() << "Sleeping for " << left << " ms" << reportEnd();
                    std::this_thread::sleep_for(std::chrono::milliseconds(left));
                    timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::high_resolution_clock::now() - previousTimestampTime);
                }
                previousTimestamp = timestamp;
                previousTimestampTime = std::chrono::high_resolution_clock::now();
            }

            addOutputData(0, dataFrame);

            if(!mFirstFrameIsInserted) {
                {
                    std::lock_guard<std::mutex> lock(mFirstFrameMutex);
                    mFirstFrameIsInserted = true;
                }
                mFirstFrameCondition.notify_one();
            }
            if(mSleepTime > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(mSleepTime));
            i += mStepSize;
            if(i == mMaximumNrOfFrames) {
                throw FileNotFoundException();
            }

        } catch(FileNotFoundException &e) {
            if(i > 0) {
                reportInfo() << "Reached end of stream" << Reporter::end();
                // If there where no files found at all, we need to release the execute method
                if(!mFirstFrameIsInserted) {
                    {
                        std::lock_guard<std::mutex> lock(mFirstFrameMutex);
                        mFirstFrameIsInserted = true;
                    }
                    mFirstFrameCondition.notify_one();
                }
                if(mLoop ||
                   (mNrOfReplays > 0 && replays != mNrOfReplays) ||
                   (currentSequence < mFilenameFormats.size()-1)) {
                    // Restart stream
                    previousTimestamp = 0;
                    previousTimestampTime = std::chrono::high_resolution_clock::time_point::min();
                    if(timestampFile.is_open()) {
                        timestampFile.seekg(0); // reset file to start
                    }
                    replays++;
                    i = mStartNumber;
                    currentSequence++;
                    // Go to first sequence if looping is enabled
                    if(mLoop && currentSequence == mFilenameFormats.size()) {
                        currentSequence = 0;
                    }
                    continue;
                }
                mHasReachedEnd = true;
                // Reached end of stream
                break;
            } else {
                throw e;
            }
        } catch(ThreadStopped &e) {
            break;
        }
    }
}

std::string FileStreamer::getFilename(uint i, int currentSequence) const {
    std::string filename = mFilenameFormats[currentSequence];
    std::string frameNumber = std::to_string(i);
    if(mZeroFillDigits > 0 && frameNumber.size() < mZeroFillDigits) {
            std::string zeroFilling = "";
            for(uint z = 0; z < mZeroFillDigits - frameNumber.size(); z++) {
                zeroFilling += "0";
            }
            frameNumber = zeroFilling + frameNumber;
        }
    filename.replace(
                filename.find("#"),
                1,
                frameNumber
        );
    return filename;
}

FileStreamer::~FileStreamer() {
    if(mStreamIsStarted) {
        if(mThread->get_id() != std::this_thread::get_id()) { // avoid deadlock
            stop();
            delete mThread;
            mThread = NULL;
        }
    }
}

bool FileStreamer::hasReachedEnd() {
    return mHasReachedEnd;
}

void FileStreamer::setStartNumber(uint startNumber) {
    mStartNumber = startNumber;
}

void FileStreamer::setZeroFilling(uint digits) {
    mZeroFillDigits = digits;
}

void FileStreamer::enableLooping() {
    mLoop = true;
}

void FileStreamer::disableLooping() {
    mLoop = false;
}

void FileStreamer::setStepSize(uint stepSize) {
    if(stepSize == 0)
        throw Exception("Step size given to FileStreamer can't be 0");
    mStepSize = stepSize;
}

void FileStreamer::stop() {
    {
        std::unique_lock<std::mutex> lock(mStopMutex);
        mStop = true;
    }
    mThread->join();
    reportInfo() << "File streamer thread returned" << reportEnd();
}

void FileStreamer::setUseTimestamp(bool use) {
    mUseTimestamp = use;
}

} // end namespace fast
