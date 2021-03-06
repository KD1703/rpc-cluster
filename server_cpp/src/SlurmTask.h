#pragma once
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include "FromChars.h"
#include "IOutputStream.h"
#include "ITaskManager.h"
#include "Logger.h"
#include "cluster.pb.h"
#include "Point.h"

class SlurmTask
{
public:
    SlurmTask(IOutputStream* stream, const std::string& pathToMath, boost::iterator_range<const double*> arguments);
    template<class Callable>
    void StartAsync(Callable&& callback)
    {
        m_process = boost::process::child(
            m_runBinCommand,
            boost::process::std_out > m_pipe
        );
        cmd::log << "New task started up" << std::endl;
        boost::asio::async_read_until(m_pipe, m_buffer, '\n', m_readHandler);
        std::thread([this, &callback]()
        {
            try
            {
                m_ioService.run();
            }
            catch (boost::wrapexcept<boost::system::system_error>& e)
            {
                cmd::log << e.what() << std::endl;
            }
            m_process.join();
            if (m_process.exit_code() == 0)
                callback();
        }).detach();
        cmd::log << "IoService started up" << std::endl;
    }

    cluster::PointBatch Suspend();
    void Resume();
    ~SlurmTask();
private:
    std::mutex m_bufSync;
    boost::process::child m_process;
    boost::asio::streambuf m_buffer;
    boost::asio::io_service m_ioService;
    boost::process::async_pipe m_pipe;

    IOutputStream* m_stream;
    std::string m_runBinCommand;
    std::function<void(const boost::system::error_code & e, std::size_t size)> m_readHandler;

    template<bool extractSymbols = false>
    cluster::PointBatch ReadPointsFromBuffer()
    {
        cluster::PointBatch points;
        RawPoint point{};
        std::lock_guard<std::mutex> lock(m_bufSync);
        if constexpr (extractSymbols)
        {
            std::istream is(&m_buffer);
            while (is >> point.x >> point.y >> point.z)
            {
                *points.add_point() = point;
                if (std::iswcntrl(is.peek()))
                {
                    while (std::isspace(is.peek()))
                    {
                        is.get();
                    }
                    break;
                }
            }
        }
        else
        {
            auto stringBegin = reinterpret_cast<const char*>(m_buffer.data().data());
            while(std::isspace(*stringBegin))
            {
                stringBegin++;
            }
            const auto stringEnd = stringBegin + m_buffer.data().size();
            for (;;)
            {
                auto convertResult = std::from_chars(stringBegin, stringEnd, point.x);
                if (convertResult.ec != std::errc())
                    break;
                convertResult = std::from_chars(convertResult.ptr, stringEnd, point.y);
                if (convertResult.ec != std::errc())
                    break;
                convertResult = std::from_chars(convertResult.ptr, stringEnd, point.z);
                if (convertResult.ec != std::errc())
                    break;
                *points.add_point() = point;
            }
        }
        return points;
    }
};

