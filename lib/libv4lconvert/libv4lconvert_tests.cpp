#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <gtest/gtest.h>

#include "libv4lconvert.h"
#include "libv4l-plugin.h"

template<typename T>
void initWithZero(T* t)
{
    std::memset(t, 0, sizeof(T));
}

class V4L2Device
{
public:
    int ioctl(unsigned long int request, void *arg)
    {
    switch (request)
    {
	case VIDIOC_QUERYCAP:
	    return ioctlQuerycap(static_cast<v4l2_capability *>(arg));

	default:
	    std::clog << "Unknown ioctl: " << request << std::endl;
	    errno = EINVAL;
	    return -1;
	}
    }

    ssize_t read(void *buffer, size_t n)
    {
	errno = ENXIO;
	return -1;
    }
    
    ssize_t write(const void *buffer, size_t n)
    {
	errno = ENXIO;
	return -1;
    }

public:
    int ioctlQuerycap(v4l2_capability* cap)
    {
	errno = EINVAL;
	return -1;
    }
};

class V4L2MockDevice : public V4L2Device
{
    int ioctlQuerycap(v4l2_capability* cap)
    {
	initWithZero(cap);
	
	std::strcat(reinterpret_cast<char*>(cap->driver), "Mock Driver");
	std::strcat(reinterpret_cast<char*>(cap->card), "Mock Card");
	std::strcat(reinterpret_cast<char*>(cap->bus_info), "PCI:0000:05:06.0");
	cap->version = 0; // KERNEL_VERSION(3, 0, 0);
	
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;

	errno = ENOTTY;
	return -1;
    }
};

class V4L2DeviceRegistry
{

public:
    libv4l_dev_ops* getDevOps() { return &m_devOps; }

    void registerDevice(int fd, V4L2Device* pDevice)
    {
	m_devices[fd] = pDevice;
    }

private:
    static void* ops_init(int fd)
    {
	DeviceMap::iterator it = m_devices.find(fd);
	if (it == m_devices.end()) return 0;
	
	return it->second;
    }

    static void ops_close(void *dev_ops_priv)
    {
    }

    static int ops_ioctl(void *dev_ops_priv, int fd, unsigned long int request, void *arg)
    {
	V4L2Device* const pDevice = static_cast<V4L2Device*>(dev_ops_priv);
	return pDevice->ioctl(request, arg);
    }

    static ssize_t ops_read(void *dev_ops_priv, int fd, void *buffer, size_t n)
    {
	V4L2Device* const pDevice = static_cast<V4L2Device*>(dev_ops_priv);
	return pDevice->read(buffer, n);
    }

    static ssize_t ops_write(void *dev_ops_priv, int fd, const void *buffer, size_t n)
    {
	V4L2Device* const pDevice = static_cast<V4L2Device*>(dev_ops_priv);
	return pDevice->write(buffer, n);
    }

    static libv4l_dev_ops m_devOps;

    typedef std::map<int, V4L2Device*> DeviceMap;
    static DeviceMap m_devices;
};

libv4l_dev_ops V4L2DeviceRegistry::m_devOps = {
	V4L2DeviceRegistry::ops_init,
	V4L2DeviceRegistry::ops_close,
	V4L2DeviceRegistry::ops_ioctl,
	V4L2DeviceRegistry::ops_read,
	V4L2DeviceRegistry::ops_write,
    };

V4L2DeviceRegistry::DeviceMap V4L2DeviceRegistry::m_devices;

TEST(libv4lconvert, colorspace_conversion)
{
    V4L2MockDevice* pDevice = new V4L2MockDevice;
    V4L2DeviceRegistry registry;
    registry.registerDevice(42, pDevice);

    v4lconvert_data* pData = ::v4lconvert_create_with_dev_ops(42, pDevice, registry.getDevOps());
    ASSERT_TRUE(pData);
    
    v4lconvert_destroy(pData); pData = NULL;
}