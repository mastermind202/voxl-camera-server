/*******************************************************************************
 * Copyright 2022 ModalAI Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 4. The Software is used solely in conjunction with devices provided by
 *    ModalAI Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#ifndef TOF_BRIDGE_H
#define TOF_BRIDGE_H

#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <vector>
#include <utility>

#include <utils/Errors.h>

// royale libraries
#include <royale/SparsePointCloud.hpp>
#include <royale/DepthData.hpp>
#include <royale/IRImage.hpp>
#include <royale/DepthImage.hpp>
#include <royale/ExposureMode.hpp>
#include <platform/CameraFactory.hpp>
#include <platform/ModuleConfigCustom.hpp>
#include <hal/IBridgeImager.hpp>
#include <hal/IBridgeDataReceiver.hpp>
#include <royale/IIRImageListener.hpp>
#include <pal/II2cBusAccess.hpp>
#include <royale/ICameraDevice.hpp>

// local includes
#include "cci_direct.h"


// -----------------------------------------------------------------------------------------------------------------------------
// 
// -----------------------------------------------------------------------------------------------------------------------------
#define TOF_1PHASE_WIDTH        224
#define TOF_1PHASE_HEIGHT       172
#define TOF_2PHASE_WIDTH        224
#define TOF_2PHASE_HEIGHT       346
#define TOF_3PHASE_WIDTH        224
#define TOF_3PHASE_HEIGHT       519
#define TOF_4PHASE_WIDTH        224
#define TOF_4PHASE_HEIGHT       692
#define TOF_5PHASE_WIDTH        224
#define TOF_5PHASE_HEIGHT       865
#define TOF_9PHASE_WIDTH        224
#define TOF_9PHASE_HEIGHT       1557
#define TOF_10PHASE_WIDTH       224
#define TOF_10PHASE_HEIGHT      1730
#define TOF_11PHASE_WIDTH       224
#define TOF_11PHASE_HEIGHT      1903

#define PROPERTY_VALUE_MAX 32

// -----------------------------------------------------------------------------------------------------------------------------
// 
// -----------------------------------------------------------------------------------------------------------------------------
using namespace royale::pal;
using namespace royale::hal;
using namespace android;


// -----------------------------------------------------------------------------------------------------------------------------
// 
// -----------------------------------------------------------------------------------------------------------------------------
typedef enum {
    SHORT_RANGE      = 5,
    LONG_RANGE       = 9,
    EXTRA_LONG_RANGE = 11,
} RoyaleDistanceRange;

typedef enum {
    FRAME_RATE      = 1<<0,
    DISTANCE_RANGE  = 1<<1,
    EXPOSURE_MODE   = 1<<2,
    EXPOSURE_TIME   = 1<<3,
    EXPOSURE_LIMITS = 1<<4,
} RoyaleParamChange;

typedef enum {
    LISTENER_NONE               = 0x0,
    LISTENER_DEPTH_DATA         = 0x1,
    LISTENER_SPARSE_POINT_CLOUD = 0x2,
    LISTENER_DEPTH_IMAGE        = 0x4,
    LISTENER_IR_IMAGE           = 0x8
} RoyaleListenerType;

enum I2C_REG_ADDR_TYPE
{
    TOF_I2C_ADDR_TYPE_BYTE,
    TOF_I2C_ADDR_TYPE_WORD,
    TOF_I2C_ADDR_TYPE_3B
};
enum I2C_DATA_TYPE
{
    TOF_I2C_DATA_TYPE_BYTE,
    TOF_I2C_DATA_TYPE_WORD,
    TOF_I2C_DATA_TYPE_DWORD
};
struct I2CRegArray
{
    unsigned short regAddr;
    unsigned short regData;
    unsigned int delay;
};

// -----------------------------------------------------------------------------------------------------------------------------
// This is the listener client that the TOF bridge library will call when it has post processed data from the Royale PMD libs
// -----------------------------------------------------------------------------------------------------------------------------
class IRoyaleDataListener {
    public:

        // int dumpDepthFrames();
        // TODO: figure out why this interace was causing an abstract issue
        virtual bool royaleDataDone(void* pData, uint32_t size, int64_t timestamp, RoyaleListenerType dataType) = 0;
};

// -----------------------------------------------------------------------------------------------------------------------------
// 
// -----------------------------------------------------------------------------------------------------------------------------
#define MAX_ALLOWED_CAMERAS        6
#define MAX_CAMERAS                (5)  // 0-5
#define CAM_ID_INVALID             (255)
#define SUBDEV_CAM_SENSOR_DRIVER   "cam-sensor-driver"

enum camera_sensor_i2c_type {
    CAMERA_SENSOR_I2C_TYPE_INVALID,
    CAMERA_SENSOR_I2C_TYPE_BYTE,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_3B,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    CAMERA_SENSOR_I2C_TYPE_MAX,
};

struct cam_sensor_i2c_reg_array {
    uint32_t reg_addr;
    uint32_t reg_data;
    uint32_t delay;
    uint32_t data_mask;
};

struct cam_sensor_i2c_reg_setting {
    struct cam_sensor_i2c_reg_array *reg_setting;
    uint32_t size;
    enum camera_sensor_i2c_type addr_type;
    enum camera_sensor_i2c_type data_type;
    unsigned short delay;
    uint8_t *read_buff;
    uint32_t read_buff_len;
};

struct cam_sensor_cci_direct_read_setting {
	struct cam_sensor_i2c_reg_array *reg_setting;
	uint32_t size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
	uint32_t *read_buff;
	uint32_t read_buff_len;
};

// TODO: include cam_sensor_cmn_header.h in system image
struct cam_cci_direct_reg_setting {
	uint32_t dev_addr;
	struct cam_sensor_i2c_reg_array *reg_setting;
	uint32_t size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
	uint32_t *read_buff;
};


// enum class I2cAddressMode {
//     I2C_NO_ADDRESS,
//     I2C_8BIT,
//     I2C_16BIT
// };

class I2cAccess : public royale::pal::II2cBusAccess {
    public:
        I2cAccess(int cameraId) {m_cameraId = cameraId;}
        ~I2cAccess();

        int setup();

        void writeI2c (uint8_t devAddr, I2cAddressMode addrMode, uint16_t regAddr, const std::vector<uint8_t> &buffer);
        void writeI2cArray (uint8_t devAddr, I2cAddressMode addrMode, const std::map <uint16_t, uint16_t> &reg_map);

        void readI2c (uint8_t devAddr, I2cAddressMode addrMode, uint16_t regAddr, std::vector<uint8_t> &buffer);
        void readI2cSeq (uint8_t devAddr, uint16_t regAddr, I2cAddressMode addrMode, std::vector<uint8_t> &data, I2C_DATA_TYPE dataType);

        int setGPIO(uint16_t gpio, uint16_t data);

        // functions required by interface, dummy implementations
        void setBusSpeed (uint32_t bps) { } 
        std::size_t maximumDataSize () { return 10000; }

    
    private:
        int m_cameraId;
};

// -----------------------------------------------------------------------------------------------------------------------------
// Contains the data from one capture  
// -----------------------------------------------------------------------------------------------------------------------------
class CapturedBuffer : public royale::hal::ICapturedBuffer {
    public:
        CapturedBuffer (uint16_t *buffer, uint64_t timeStamp)
            : mDataBuffer(buffer), mTimestamp(timeStamp)
        {
        }
        ~CapturedBuffer() { }

        uint16_t* getPixelData()override { return mDataBuffer; }
        std::size_t getPixelCount() override { return TOF_9PHASE_WIDTH * TOF_9PHASE_HEIGHT; }
        uint64_t getTimeMicroseconds() override { return mTimestamp; }

    private:
        uint16_t *mDataBuffer;
        uint64_t mTimestamp;
};


// -----------------------------------------------------------------------------------------------------------------------------
// Bridge interface functions for reading from main data-capture source
// TODO: we might potentially not need this is we're using hal3 in order to do
// all the reading
// -----------------------------------------------------------------------------------------------------------------------------
class BridgeDataReceiver : public royale::hal::IBridgeDataReceiver {
    public:
        BridgeDataReceiver() {};
        ~BridgeDataReceiver() {};

        void setBufferCaptureListener(royale::hal::IBufferCaptureListener *collector) override;
        std::size_t executeUseCase(int width, int height, std::size_t preferredBufferCount) override;
        void startCapture() override;
        void stopCapture() override;
        float getPeakTransferSpeed() override;
        void queueBuffer(royale::hal::ICapturedBuffer *buffer) override;

        void dataCallback(uint16_t * pixelData, uint64_t ts);
        bool isConnected() const override;
        royale::Vector<royale::Pair<royale::String, royale::String>> getBridgeInfo() override;
        void setEventListener (royale::IEventListener *listener) override;

    private:
        royale::hal::IBufferCaptureListener *m_bufferCaptureListener;
        std::mutex m_changeListenerLock;
};


// -----------------------------------------------------------------------------------------------------------------------------
// Bridge Imager
// -----------------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
// EEPROM Header Data Format for LiteOn A65 & A66 (aka A65 v1.1)
// Version 7
// Key:
//    P      = position
//    Cnt    = count
//    SoD    = Size of Data
//    [...]  = calibration data
//
// |         | P | Name                 | Type       | Cnt | Example             | Bytes |
// | ------- | - | -------------------- | ---------- | --- | ------------------- | ----- |
// | Header  | 1 | magic = PMDTEC       | char       | 6   | PMDTEC              | 6     |
// |         | 2 | version = 7          | uint16_t   | 1   | 7                   | 2     |
// |         | 3 | CRC32 of Data        | uint32_t   | 1   | 4181266073          | 4     |
// |         | 4 | Size of Data         | uint32_t   | 1   | 96391               | 4     |
// |         | 5 | Product Identifier   | uint8_t    | 16  | (16 bytes of data)  | 16    |
// |         | 6 | Product Suffix       | char + pad | 16  | 105 outdoor         | 16    |
// |         | 7 | Serial Number String | char       | 19  | 3fab-160b-af35-ab21 | 16    |
// | Data    | 8 | Calibration Data     | uint8_t    | SoD | [...]               | SoD   |
//----------------------------------------------------------------------------------------


//----------------------------------------------------------------------------------------
// Version 7 of header
//----------------------------------------------------------------------------------------
typedef union {
    struct {
        char      magic[6];
        uint16_t  version;
        uint32_t  data_crc32;
        uint32_t  data_size;
        uint8_t   product_id[16];
        char      product_suffix[16];
        char      serial_number[19];
    } __attribute__((packed));
    uint8_t data[67];
} calDataHeaderv7_t;


//----------------------------------------------------------------------------------------
// Old header
//----------------------------------------------------------------------------------------
typedef union {
    struct {
        char magic[6];
        uint16_t version;
        uint32_t checksum;
        uint32_t size;
    };
    uint8_t data[16];
} calDataHeader_t;


//----------------------------------------------------------------------------------------
// Bridge interface for controlling sensor hardware
//----------------------------------------------------------------------------------------
#define CALIBRATION_LENS_SIZE 44
#define CALIBRATION_EFFICIENCY_SIZE 8

#define EEPROM_1ST_PAGE_ADDR 0x56
#define EEPROM_PAGE_SIZE 65536
#define EEPROM_PAGE_NUM 2

class BridgeImager : public royale::hal::IBridgeImager {
  public:
    BridgeImager(std::shared_ptr<I2cAccess> i2cAccess);
    ~BridgeImager() { };

    // R/W functions used by royale IBridgeImager to communicate with ToF
    void readImagerRegister(uint16_t regAddr, uint16_t &value);
    void writeImagerRegister(uint16_t regAddr, uint16_t value);
    void readImagerBurst(uint16_t firstRegAddr, std::vector<uint16_t> &values);
    void writeImagerBurst(uint16_t firstRegAddr, const std::vector<uint16_t> &values);

    // utility for sleeping x microseconds
    void sleepFor (std::chrono::microseconds sleepDuration);

    // Not implemented, dummy virtual definition
    void setImagerReset(bool state) { };

    static const uint8_t imagerSlave = 0x7A; // 0x3D << 1

  private:

    bool calFileExist();
    bool calDataParse();
    void calEepromRead();
    bool getEepromHeaderVersion(int16_t& version);

    void calFileDump();
    void calEepromDumpToFile();

    // Functions for validating EEPROM data for different header versions
    bool calDataValidate(std::vector<uint8_t> &data);
    bool calDataValidatev7(std::vector<uint8_t> &data);

    uint32_t crc32(uint32_t crc, const uint8_t *buf, size_t size);

    // Currently no GPIO reset available for camera
    // bool m_GPIOexported;
    // int32_t m_ResetGpioPin;
    // std::ofstream m_GPIOexport;

    std::shared_ptr<I2cAccess> m_i2cAccess;

    // calibration data storage
    std::vector<uint8_t> calEepromData;
    std::vector<uint8_t> calDataUnknown;
    std::vector<uint8_t> calDataLens;
    std::vector<uint8_t> calDataEfficiency;

    // calibration files strings
    const std::string calEepromFileNamePrivate = "/data/misc/camera/pmd.spc";
    const std::string calEepromFileNameTango   = "/data/misc/camera/tango.bin";
    const std::string calEepromFileNameModule  = "/data/misc/camera/scale.spc";
    const std::string calEepromFileNameDump    = "/data/misc/camera/tof_cal_eeprom.bin";
};


// -----------------------------------------------------------------------------------------------------------------------------
// Abstraction layer for Royale's API
// -----------------------------------------------------------------------------------------------------------------------------
class TOFBridge {

    class IRImageListener : public royale::IIRImageListener {
        public:
        IRImageListener(TOFBridge* tofBridge) { mTOFBridge = tofBridge; }
        ~IRImageListener() {}
        void onNewData (const royale::IRImage *data);

        private:
            TOFBridge* mTOFBridge;
    };

    class DepthImageListener : public royale::IDepthImageListener {
        public:
        DepthImageListener(TOFBridge* tofBridge) { mTOFBridge = tofBridge; }
        ~DepthImageListener() {}
        void onNewData (const royale::DepthImage *data);

        private:
            TOFBridge* mTOFBridge;
    };

    class SparsePointCloudListener : public royale::ISparsePointCloudListener {
        public:
        SparsePointCloudListener(TOFBridge* tofBridge) { mTOFBridge = tofBridge; }
        ~SparsePointCloudListener() {}
        void onNewData (const royale::SparsePointCloud *data);

        private:
            TOFBridge* mTOFBridge;
    };

    class DepthDataListener : public royale::IDepthDataListener {
        public:
        DepthDataListener(TOFBridge* tofBridge) { mTOFBridge = tofBridge; printf("I MADE IT HEREEREE\n\n\n\n");}
        ~DepthDataListener() {}

        void onNewData (const royale::DepthData *data);

        private:
            TOFBridge* mTOFBridge;
    };

    friend class IRImageListener;
    friend class DepthImageListener;
    friend class SparsePointCloudListener;
    friend class DepthDataListener;

    public:
        TOFBridge();
        virtual ~TOFBridge();
        virtual status_t setup();
        static android::status_t populateSupportedUseCases(std::vector<uint8_t> &long_range, std::vector<uint8_t> &short_range,
                                                  uint8_t & default_range, uint8_t & default_data_output,
                                                  std::pair<int64_t,int64_t>& default_exp_time_limits,
                                                  uint32_t & default_fps, uint32_t & default_exp_time  );
        
        static bool isTOFCam(int32_t width, int32_t height);

        status_t startCapture();
        status_t stopCapture();
        void getFrameRateListShortRange(std::vector<uint8_t> &list);
        void getFrameRateListLongRange(std::vector<uint8_t> &list);
        void setFrameRate(uint8_t frameRate);
        uint8_t getFrameRate();
        void setDistanceRange(RoyaleDistanceRange distanceRange);
        RoyaleDistanceRange getDistanceRange();

        void setExposureTime(uint32_t expTime);
        uint32_t getExposureTime();
        void setExposureMode(royale::ExposureMode expMode);
        royale::ExposureMode getExposureMode();
        std::pair<uint32_t, uint32_t> getExposureLimits();
        void setChange(RoyaleParamChange param);
        bool getChange(RoyaleParamChange param);
        void clearChange(RoyaleParamChange param);

        // // set _and_ initialize the required depth data listener
        void setInitDataOutput(RoyaleListenerType _dataOutput);

        void addRoyaleDataListener(IRoyaleDataListener * ptrChannel) {
            mDepthChannel = ptrChannel;
        }

        void dataCallback(uint16_t * pixelData, uint64_t ts) {
            bridgeReceiver->dataCallback(pixelData, ts);
        }

        static std::vector <uint32_t> mShortRangeFramerates;
        static std::vector <uint32_t> mLongRangeFramerates;
        static std::vector <uint32_t> mExtraLongRangeFramerates;

        int setUseCase(RoyaleDistanceRange range, uint8_t frameRate);
    
    private:

        I2cAccess *mPtrI2CHAL;
        std::shared_ptr<I2cAccess> i2cAccess;
        std::shared_ptr<royale::hal::IBridgeImager> bridgeImager;
        std::shared_ptr<BridgeDataReceiver> bridgeReceiver;
        std::unique_ptr<royale::ICameraDevice> royaleCamera;
        std::shared_ptr<royale::config::ModuleConfig> moduleConfig;

        int32_t dataOutput;
        uint8_t mFrameRate;
        RoyaleDistanceRange mDistanceRange;
        royale::Pair<uint32_t, uint32_t> mExposureLimits;
        uint32_t mExposureTime;
        royale::ExposureMode mExposureMode;
        royale::String mUseCaseName;
        uint8_t paramChange;
        std::mutex paramChangeLock;

        struct {
            IRImageListener *irImage;
            DepthImageListener *depthImage;
            SparsePointCloudListener *sparsePointCloud;
            DepthDataListener *depthData;
        } listeners;

        status_t onRoyaleDepthData(const void *data, uint32_t size, int64_t timestamp, RoyaleListenerType dataType);
        royale::usecase::UseCaseDefinition *getUseCaseDef (royale::String useCaseName);
        uint32_t getExposureTime(royale::String useCaseName);

        IRoyaleDataListener *mDepthChannel;
};


// -----------------------------------------------------------------------------------------------------------------------------
// TOF interface
// -----------------------------------------------------------------------------------------------------------------------------
struct TOFInitializationData {
    void*                 pTOFInterface;        ///< TOF Interface pointer
    uint32_t              numDataTypes;         ///< Type of listener types
    RoyaleListenerType*   pDataTypes;           ///< RoyaleListenerType
    IRoyaleDataListener*  pListener;            ///< Class object of type IRoyaleDataListener
    uint32_t              frameRate;            ///< TOF camera Frame rate
    RoyaleDistanceRange   range;                ///< TOF mode (5, 9)
};

class TOFInterface {
    public:
        TOFInterface() { }
        ~TOFInterface() { }

        static TOFInterface* CreateInstance() { 
            return new TOFInterface(); 
        }

        int Initialize(TOFInitializationData* pTOFInitializeData);
        void ProcessTOFRAW16(uint16_t* pRaw16PixelData, uint64_t timestamp) {
            return m_pTofBridge->dataCallback(pRaw16PixelData, timestamp);
        }

    private:
        I2cAccess*        m_pI2cAccess;     ///< I2CAccess HAL
        TOFBridge*        m_pTofBridge;      ///< TOF Bridge
};

// -----------------------------------------------------------------------------------------------------------------------------
// Main interfaces for the TOF Bridge Library
// -----------------------------------------------------------------------------------------------------------------------------
extern "C" {
void* TOFCreateInterface();
int   TOFInitialize(TOFInitializationData* pTOFInitializationData);
void  TOFProcessRAW16(void* p, uint16_t* pRaw16PixelData, uint64_t timestamp);
void  TOFDestroyInterface(void* pTOFInterface);
int   TOFIsTofCam(int32_t width, int32_t height);  //returns 1 if width and height matches one of the TOF sensor resolutions
int   TOFGetFrameSize(RoyaleDistanceRange range, int32_t & width, int32_t & height);
}


#endif // TOF_BRIDGE_H
