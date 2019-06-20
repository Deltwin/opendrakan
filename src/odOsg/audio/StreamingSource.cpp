/*
 * StreamingSource.cpp
 *
 *  Created on: Jun 19, 2019
 *      Author: zal
 */

#include <odOsg/audio/StreamingSource.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include <odCore/Exception.h>

#include <odOsg/audio/SoundSystem.h>

namespace odOsg
{

    static StreamingSource::BufferFillCallback FILL_WITH_SILENCE = [](int16_t *buffer, size_t bufferSize)
    {
        std::fill(buffer, buffer+bufferSize, 0);
    };


    StreamingSource::StreamingSource(SoundSystem &ss, size_t bufferCount, size_t samplesPerBuffer)
    : Source(ss)
    , mSamplesPerBuffer(samplesPerBuffer)
    , mTempFillBuffer(std::make_unique<int16_t[]>(samplesPerBuffer))
    , mBuffers(bufferCount, nullptr)
    {
        setBufferFillCallback(nullptr);

        mBufferIds.resize(bufferCount, 0);
        for(size_t i = 0; i < bufferCount; ++i)
        {
            mBuffers[i] = od::make_refd<Buffer>(ss);
            mBufferIds[i] = mBuffers[i]->getBufferId();
        }

        std::lock_guard<std::mutex> lock(mSoundSystem.getWorkerMutex());
        alSourceQueueBuffers(mSourceId, bufferCount, mBufferIds.data());
        SoundSystem::doErrorCheck("Could not enqueue newly created buffers into streaming source");
    }

    StreamingSource::~StreamingSource()
    {
        std::lock_guard<std::mutex> lock(mSoundSystem.getWorkerMutex());

        alSourceStop(mSourceId);
        SoundSystem::doErrorCheck("Could not stop streaming source to delete it");

        alSourceUnqueueBuffers(mSourceId, mBufferIds.size(), mBufferIds.data());
        SoundSystem::doErrorCheck("Could not enqueue newly created buffers into streaming source");
    }

    void StreamingSource::setBufferFillCallback(const BufferFillCallback &callback)
    {
        if(callback == nullptr)
        {
            mFillCallback = FILL_WITH_SILENCE;

        }else
        {
            mFillCallback = callback;
        }
    }

    void StreamingSource::setSound(odDb::Sound *s)
    {
        throw od::UnsupportedException("Streaming sources can't play database sounds");
    }

    void StreamingSource::update(float relTime)
    {
        Source::update(relTime);

        std::lock_guard<std::mutex> lock(mSoundSystem.getWorkerMutex());

        ALint processedBuffers;
        alGetSourcei(mSourceId, AL_BUFFERS_PROCESSED, &processedBuffers);
        SoundSystem::doErrorCheck("Failed to query number of processed buffers of streaming source");

        if(processedBuffers == 0)
        {
            return;
        }

        // take played buffers and refill them
        for(ALint i = 0; i < processedBuffers; ++i)
        {
            auto buffer = mBuffers.front();
            mBuffers.pop_front();

            ALuint bufferId = buffer->getBufferId();

            alSourceUnqueueBuffers(mSourceId, 1, &bufferId);
            SoundSystem::doErrorCheck("Could not unqueue buffer from streaming source");

            _fillBuffer_locked(buffer, mFillCallback);

            alSourceQueueBuffers(mSourceId, 1, &bufferId);
            SoundSystem::doErrorCheck("Could not queue buffer into streaming source");
            mBuffers.push_back(buffer);
        }
    }

    void StreamingSource::_fillBuffer_locked(Buffer *buffer, const StreamingSource::BufferFillCallback &callback)
    {
        mFillCallback(mTempFillBuffer.get(), mSamplesPerBuffer);

        alBufferData(buffer->getBufferId(), AL_FORMAT_MONO16, mTempFillBuffer.get(), mSamplesPerBuffer, mSoundSystem.getContext().getOutputFrequency());
        SoundSystem::doErrorCheck("Failed to push data from fill buffer to AL buffer");
    }
}

