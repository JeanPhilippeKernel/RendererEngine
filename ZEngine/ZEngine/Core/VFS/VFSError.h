#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    enum class VFSError : uint32_t
    {
        OK               = 0,
        NotFound         = 1,
        PermissionDenied = 2,
        AlreadyExists    = 3,
        NotADirectory    = 4,
        NotAFile         = 5,
        InvalidPath      = 6,
        Unsupported      = 7,
        IOError          = 8,
        OutOfMemory      = 9,
        Corrupted        = 10,
        Cancelled        = 11,
    };

    template <typename T>
    struct VFSResult
    {
        static VFSResult Ok(T value)
        {
            VFSResult r;
            r.m_value = std::move(value);
            r.m_error = VFSError::OK;
            return r;
        }

        static VFSResult Fail(VFSError error)
        {
            ZENGINE_VALIDATE_ASSERT(error != VFSError::OK, "Use Ok() to construct success")
            VFSResult r;
            r.m_error = error;
            return r;
        }

        bool Succeeded() const
        {
            return m_error == VFSError::OK;
        }
        bool Failed() const
        {
            return m_error != VFSError::OK;
        }
        VFSError Error() const
        {
            return m_error;
        }

        T& Value()
        {
            ZENGINE_VALIDATE_ASSERT(Succeeded(), "Accessing value of a failed VFSResult")
            return m_value;
        }
        const T& Value() const
        {
            ZENGINE_VALIDATE_ASSERT(Succeeded(), "Accessing value of a failed VFSResult")
            return m_value;
        }

    private:
        T        m_value = {};
        VFSError m_error = VFSError::OK;
    };

    // Void specialisation for operations that succeed or fail with no return value
    template <>
    struct VFSResult<void>
    {
        static VFSResult Ok()
        {
            VFSResult r;
            r.m_error = VFSError::OK;
            return r;
        }
        static VFSResult Fail(VFSError e)
        {
            VFSResult r;
            r.m_error = e;
            return r;
        }

        bool Succeeded() const
        {
            return m_error == VFSError::OK;
        }
        bool Failed() const
        {
            return m_error != VFSError::OK;
        }
        VFSError Error() const
        {
            return m_error;
        }

    private:
        VFSError m_error = VFSError::OK;
    };

} // namespace ZEngine::Core::VFS