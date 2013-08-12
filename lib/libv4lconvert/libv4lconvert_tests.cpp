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
    virtual int ioctl(unsigned long int request, void *arg)
    {
    switch (request)
    {
	case VIDIOC_QUERYCAP:
	    return ioctlQuerycap(static_cast<v4l2_capability*>(arg));

	case VIDIOC_ENUM_FMT:
		return ioctlEnumFmt(static_cast<v4l2_fmtdesc*>(arg));

	case VIDIOC_S_STD:
		return ioctlSStd(static_cast<v4l2_std_id*>(arg));

	case VIDIOC_ENUMINPUT:
		return ioctlEnuminput(static_cast<v4l2_input*>(arg));

	case VIDIOC_QUERYCTRL:
		return ioctlQueryctrl(static_cast<v4l2_queryctrl*>(arg));

	case VIDIOC_G_INPUT:
		return ioctlGInput(static_cast<int*>(arg));

	default:
	    std::clog << "Unknown ioctl"
	    		<< " nr=" << ((request & _IOC_NRMASK) >> _IOC_NRSHIFT) << std::endl;
	    errno = EINVAL;
	    return -1;
	}
    }

    virtual ssize_t read(void *buffer, size_t n)
    {
	errno = ENXIO;
	return -1;
    }
    
    virtual ssize_t write(const void *buffer, size_t n)
    {
	errno = ENXIO;
	return -1;
    }

public:
    virtual int ioctlQuerycap(v4l2_capability* cap)
    {
	errno = EINVAL;
	return -1;
    }

    virtual int ioctlEnumFmt(v4l2_fmtdesc* desc)
    {
    	errno = EINVAL;
    	return -1;
    }

    virtual int ioctlSStd(v4l2_std_id* std)
    {
    	errno = EINVAL;
    	return -1;
    }

    virtual int ioctlEnuminput(v4l2_input* input)
    {
    	errno = EINVAL;
    	return -1;
    }

    virtual int ioctlQueryctrl(v4l2_queryctrl* ctrl)
    {
    	errno = EINVAL;
    	return -1;
    }

    virtual int ioctlGInput(int* input)
    {
    	errno = EINVAL;
    	return -1;
    }
};

class V4L2LogDevice : public V4L2Device
{
public:
	V4L2LogDevice(std::ostream& output, V4L2Device& worker) : m_output(output), m_worker(worker)
	{
	}

	int ioctl(unsigned long int request, void *arg)
	{
	    m_output << "ioctl"
	    		<< " type=" << ((request & _IOC_TYPEMASK) >> _IOC_TYPESHIFT)
	    		<< " nr=" << ((request & _IOC_NRMASK) >> _IOC_NRSHIFT)
	    		<< std::endl;

	    return V4L2Device::ioctl(request, arg);
	}

	int ioctlQuerycap(v4l2_capability* cap)
	{
		m_output << "->VIDIOC_QUERYCAP" << std::endl;
		int ret = m_worker.ioctlQuerycap(cap);
		m_output << "<-VIDIOC_QUERYCAP" << std::endl;
		return ret;
    }

    int ioctlEnumFmt(v4l2_fmtdesc* desc)
    {
		m_output << "->VIDIOC_ENUM_FMT" << std::endl;
		int ret = m_worker.ioctlEnumFmt(desc);
		m_output << "<-VIDIOC_ENUM_FMT" << std::endl;
		return ret;
    }

    int ioctlSStd(v4l2_std_id* std)
    {
		m_output << "->VIDIOC_S_STD" << std::endl;
		int ret = m_worker.ioctlSStd(std);
		m_output << "<-VIDIOC_S_STD" << std::endl;
		return ret;
    }

    int ioctlEnuminput(v4l2_input* input)
    {
		m_output << "->VIDIOC_ENUMINPUT" << std::endl;
		int ret = m_worker.ioctlEnuminput(input);
		m_output << "<-VIDIOC_ENUMINPUT" << std::endl;
		return ret;
    }

    int ioctlQueryctrl(v4l2_queryctrl* ctrl)
    {
		m_output << "->VIDIOC_QUERYCTRLT" << std::endl;
		int ret = m_worker.ioctlQueryctrl(ctrl);
		m_output << "<-VIDIOC_QUERYCTRL" << std::endl;
		return ret;
    }

    int ioctlGInput(int* input)
    {
		m_output << "->VIDIOC_G_INPUT" << std::endl;
		int ret = m_worker.ioctlGInput(input);
		m_output << "<-VIDIOC_G_INPUT" << std::endl;
		return ret;
    }

protected:
	std::ostream& m_output;
	V4L2Device& m_worker;
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

	return 0;
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
    V4L2LogDevice* pLog = new V4L2LogDevice(std::clog, *pDevice);
    V4L2DeviceRegistry registry;
    registry.registerDevice(42, pLog);

    v4lconvert_data* pData = ::v4lconvert_create_with_dev_ops(42, pLog, registry.getDevOps());
    ASSERT_TRUE(pData);
    
    v4lconvert_destroy(pData); pData = NULL;
}
