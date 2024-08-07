////////////////////////////////////////////////////////////////////////////////
// Astra Mini series firmware updater, for POSIX
// =======================================================================
// (C) 2024 Raphael Kim @ bear robotics.
// 
// supported platforms : Debian Linux amd64, aarch64
//                       macOS11 universal
//                       Windows amd64
// 
////////////////////////////////////////////////////////////////////////////////

#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>

#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Error.hpp>

////////////////////////////////////////////////////////////////////////////////

using namespace std;
using namespace ob;

////////////////////////////////////////////////////////////////////////////////

#define RELEASE_P( _x_ )    delete _x_; _x_ = nullptr
#define RELEASE_A( _x_ )    delete[] _x_; _x_ = nullptr
#define APP_V_MAJ           (0)
#define APP_V_MIN           (1)
#define APP_V_PAT           (0)
#define APP_V_BLD           (21)

////////////////////////////////////////////////////////////////////////////////

static char          short_opts[] = " :hld:s:aev";
static struct option long_opts[] = {
    { "help",           no_argument,        0, 'h' },
    { "list",           no_argument,        0, 'l' },
    { "devid",          required_argument,  0, 'd' },
    { "devsn",          required_argument,  0, 's' },
    { "all",            no_argument,        0, 'a' },
    { "lessverbose",    no_argument,        0, 'e' },
    { "versions",       no_argument,        0, 'v' },
    { NULL,             0,                  0, 0 }
};

static char*        optpar_me = nullptr;
static uint8_t      optpar_all = 0;
static uint8_t      optpar_list = 0;
static uint8_t      optpar_versions = 0;
static uint8_t      optpar_lessverbose = 0;
static uint8_t      optpar_devonly = 0;
static uint8_t      optpar_devtype = 0; /// 0 == UID, 1 == SN
static char*        optpar_dev_uid = nullptr;
static char*        optpar_dev_sn = nullptr;
static char*        optpar_fwfile = nullptr;

////////////////////////////////////////////////////////////////////////////////

bool                   isWaitRebootComplete_ = false;
bool                   isDeviceRemoved_      = false;
mutex                  waitRebootMutex_;
string                 deviceUid_;
string                 deviceSN_;
shared_ptr<Device>     rebootedDevice_;

////////////////////////////////////////////////////////////////////////////////

static void releaseParams()
{
    if ( optpar_dev_uid != nullptr ) RELEASE_A( optpar_dev_uid );
    if ( optpar_dev_sn != nullptr ) RELEASE_A( optpar_dev_sn );
    if ( optpar_fwfile != nullptr ) RELEASE_A( optpar_fwfile );
}

static void showHelp()
{
    printf( "\n" );
    printf( " usage : %s [option] (parameter) [firmware file]\n", optpar_me );
    printf( "\n" );
    printf( " -h, --help        : show help (this).\n" );
    printf( " -l, --list        : enumerate detected devices.\n" );
    printf( " -d, --devid (uid) : select uid device only.\n" );
    printf( " -s, --devsn (sn)  : select device only for sn.\n" );
    printf( " -a, --all         : select all devices.\n" );
    printf( " -e, --lessverbose : make verbose lesser.\n" );
    printf( " -v, --version     : shows versions only.\n" );
    printf( "\n" );
}

static void prtDevInfo( shared_ptr<Device> device )
{
    if ( device != nullptr )
    {
        auto devInfo = device->getDeviceInfo();

        if ( devInfo != nullptr )
        {
            printf( "%s, USB=%04X:%04X:%s, SN=%s, Ver. = %s\n",
                    devInfo->name(),
                    devInfo->vid(),
                    devInfo->pid(),
                    devInfo->uid(),
                    devInfo->serialNumber(),
                    devInfo->firmwareVersion() );
        }
    }
}

static void showDevList( Context* ctx )
{
    if ( ctx == nullptr ) return;

    auto devList = ctx->queryDeviceList();
    size_t devCnt = devList->deviceCount();

    for( size_t cnt=0; cnt<devCnt; cnt++ )
    {
        auto dev = devList->getDevice( cnt );
        printf( "[%3zu] ", cnt ); 
        prtDevInfo(dev);
    }
}

bool upgradeFirmware( shared_ptr<Device> device, string firmwarePath ) 
{
    auto index     = firmwarePath.find_last_of(".img");
    bool isImgFile = index != string::npos;
    index          = firmwarePath.find_last_of(".bin");
    bool isBinFile = index != string::npos;

    if(!(isImgFile || isBinFile)) 
    {
        fprintf( stderr,
                 "Upgrade Fimware failed. invalid file: %s\n",
                 firmwarePath.c_str() );
        return false;
    }

    bool isUpgradeSuccess = false;

    try 
    {
        device->deviceUpgrade(
            firmwarePath.c_str(),
            [=, &isUpgradeSuccess]( OBUpgradeState state, 
                                    const char *message, 
                                    uint8_t percent) 
            {
                printf( "%s (state: %d, percent: %u %% \n",
                        message,
                        state,
                        percent );

                if(state == STAT_DONE)
                    isUpgradeSuccess = true;
            },
            false
        );
    }
    catch(Error &e) 
    {
        fprintf( stderr, "\nError type %u occured\n"
                         " .. function %s, args %s, msg : %s\n",
                         e.getExceptionType(),
                         e.getName(),
                         e.getArgs(),
                         e.getMessage() );

    }
    catch(exception &e) 
    {
        if(e.what()) 
        {
            fprintf( stderr, "\nException occured: %s\n",
                             e.what() );
        }
    }

    return isUpgradeSuccess;
}

int main(int argc, char **argv) 
{
    optpar_me = argv[0];

    size_t par_parsed = 0;

    // get options -
    for(;;)
    {
        int optidx = 0;
        int opt = getopt_long( argc, argv,
                               short_opts,
                               long_opts, &optidx );
        if ( opt >= 0 )
        {
            switch( (char)opt )
            {
                default:
                case 'h':
                    showHelp();
                    releaseParams();
                    return 0;

                case 'a':
                    optpar_all = 1;
                    optpar_devonly = 0;
                    par_parsed++;
                    break;

                case 'l':
                    optpar_list = 1;
                    par_parsed++;
                    break;

                case 'e':
                    optpar_lessverbose = 1;
                    break;

                case 'v':
                    optpar_versions = 1;
                    par_parsed++;
                    break;

                case 'd':
                {
                    if ( optarg != nullptr )
                    {
                        optpar_dev_uid = strdup( optarg ); 
                        optpar_all = 0;
                        optpar_devonly = 1;
                        optpar_devtype = 0;
                        par_parsed++;
                    }
                }
                break;

                case 's':
                {
                    if ( optarg != nullptr )
                    {
                        optpar_dev_sn = strdup( optarg );
                        optpar_all = 0;
                        optpar_devonly = 1;
                        optpar_devtype = 1;
                        par_parsed++;
                    }
                }
                break;
            }
        }
        else
            break;
    } /// of for( ;; )

    for( ; optind<argc; optind++ )
    {
        if ( optpar_fwfile == nullptr )
        {
            optpar_fwfile = strdup( argv[optind] );
        }
    }

    if ( optpar_versions > 0 )
    {
        printf( "app.version=%u.%u.%u.%u\n"
                "sdk.version=%u.%u.%u\n",
                APP_V_MAJ, APP_V_MIN, APP_V_PAT, APP_V_BLD,
                Version::getMajor(),
                Version::getMinor(),
                Version::getPatch() );
        releaseParams();
        return 0;
    }

    if ( optpar_lessverbose > 0 )
    {
        printf( "AstraFWU, version %u.%u.%u.%u, %s\n",
                APP_V_MAJ, APP_V_MIN, APP_V_PAT, APP_V_BLD,
                "(C)2024 Raph.K@BearRobotics.\n" );
    }

    if ( par_parsed == 0 )
    {
        showHelp();
        releaseParams();
        return 0;
    }

    // supress messy logs ...
    Context::setLoggerSeverity( OBLogSeverity::OB_LOG_SEVERITY_NONE );

    // Orbbec SDK Context init here.
    Context ctx;
    ctx.setDeviceChangedCallback( [](shared_ptr<DeviceList> removedList, shared_ptr<DeviceList> addedList) 
    {
        if( isWaitRebootComplete_ ) 
        {
            if( addedList && addedList->deviceCount() > 0 ) 
            {
                for( size_t x=0; x<addedList->deviceCount(); x++ )
                {
                    auto device = addedList->getDevice( x );
                    string cmps = device->getDeviceInfo()->serialNumber();

                    if( isDeviceRemoved_ && deviceSN_ == cmps )
                    {
                        rebootedDevice_       = device;
                        isWaitRebootComplete_ = false;

                        unique_lock<mutex> lk(waitRebootMutex_);
                        break;
                    }
                }
            }

            if( removedList && removedList->deviceCount() > 0 ) 
            {
                for( size_t x=0; x<removedList->deviceCount(); x++ )
                {
                    string cmps = removedList->uid( x );
                    if( deviceUid_ == cmps )
                    {
                        isDeviceRemoved_ = true;
                        break;
                    }
                }
            }
        }  /// of - if isWaitRebootComplete_
    });

    // list option ?
    if ( optpar_list > 0 )
    {
        showDevList( &ctx );
        releaseParams();
        return 0;
    }

    auto devList = ctx.queryDeviceList();
    size_t devCnt = devList->deviceCount();

    // Get the number of connected devices
    if( devCnt == 0 )
    {
        fprintf( stderr, "device not found.\n" );
        // don't return to error.
        return 0;
    }

    vector< size_t > fw_dev_lists;

    // find options ...
    if ( optpar_all > 0 )
    {
        for( size_t cnt=0; cnt<devCnt; cnt++ )
        {
            fw_dev_lists.push_back( cnt );
        }
    }
    else
    if ( optpar_devonly > 0 )
    {
        for( size_t cnt=0; cnt<devCnt; cnt++ )
        {
            auto dev = devList->getDevice(cnt);
            auto devInf = dev->getDeviceInfo();

            if ( optpar_devtype == 0 )
            {
                string dev_uid = devInf->uid();
                if ( dev_uid == optpar_dev_uid )
                {
                    fw_dev_lists.push_back( cnt );
                }
            }
            else
            {
                string dev_sn = devInf->serialNumber();
                if ( dev_sn == optpar_dev_sn )
                {
                    fw_dev_lists.push_back( cnt );
                }
            }
        }
    }

    if ( fw_dev_lists.size() > 0 )
    {
        // test firmware 
        if ( ( optpar_fwfile == nullptr ) || 
             ( access( optpar_fwfile, 0 ) != 0 ) )
        {
            fprintf( stderr, "Cannot access firmware file : %s\n",
                     optpar_fwfile );
            showHelp();
            releaseParams();
            return 0;
        }
    }

    for ( size_t cnt=0; cnt<fw_dev_lists.size(); cnt++ )
    {
        auto dev = devList->getDevice(cnt);
        printf( "Starting FW update -> " );
        prtDevInfo(dev);

        // Store uid to wait device reboot
        {
            auto devInfo = dev->getDeviceInfo();
            deviceUid_   = string(devInfo->uid());
            deviceSN_    = string(devInfo->serialNumber());
        }

        // Upgrade firmware file Path
        if(!upgradeFirmware(dev, optpar_fwfile)) 
        {
            fprintf( stderr, "firmware upgrading failure.\n" );
            return -1;
        }

        printf( "upgraded : rebooting -> " );

        isDeviceRemoved_      = false;
        isWaitRebootComplete_ = true;
        dev->reboot();
        dev     = nullptr;
        devList = nullptr;

        // wait reboot complete
        {
            unique_lock<mutex> lk(waitRebootMutex_);
        }

        // Check is reboot complete
        if(rebootedDevice_) 
        {
            printf( "Ok.\n" );
        }
        else
        {
            printf( "Failure.\n" );
        }
    }

    releaseParams();
    return 0;
}

