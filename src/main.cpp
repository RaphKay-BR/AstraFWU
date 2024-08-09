////////////////////////////////////////////////////////////////////////////////
// Astra Mini series firmware updater, for MinGW-W64 (Windows)
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
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>

// OrbbecSDK actually not support ONLY-C method ... nonsense.
extern "C" {
#include <libobsensor/ObSensor.h>
}

////////////////////////////////////////////////////////////////////////////////

using namespace std;

////////////////////////////////////////////////////////////////////////////////

#define RELEASE_P( _x_ )    delete _x_; _x_ = nullptr
#define RELEASE_A( _x_ )    delete[] _x_; _x_ = nullptr
#define RELEASE_AA( _x_ )   if ( _x_ != nullptr ) RELEASE_A( _x_ )
#define APP_V_MAJ           (0)
#define APP_V_MIN           (1)
#define APP_V_PAT           (2)
#define APP_V_BLD           (30)

////////////////////////////////////////////////////////////////////////////////

static char          short_opts[] = " :hld:s:p:aev";
static struct option long_opts[] = {
    { "help",           no_argument,        0, 'h' },
    { "list",           no_argument,        0, 'l' },
    { "devid",          required_argument,  0, 'd' },
    { "devsn",          required_argument,  0, 's' },
    { "pid",            required_argument,  0, 'p' },
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
// devtype : 
// 0 == UID, 1 == SN, 2 == USB PID
static uint8_t      optpar_devtype = 0; 
static char*        optpar_dev_uid = nullptr;
static char*        optpar_dev_sn = nullptr;
static char*        optpar_dev_usbpid = nullptr;
static char*        optpar_fwfile = nullptr;

////////////////////////////////////////////////////////////////////////////////
// Orbbec SDK related -

bool                is_wait_reboot_complete_    = false;
bool                is_device_removed_          = false;
bool                is_upgrade_success_         = false;
ob_device*          rebooted_device_            = nullptr;
int                 cb_ud                       = 0;
condition_variable  wait_reboot_condition_;
mutex               wait_reboot_mutex_;
char                device_uid_[128]            = { 0 };
char                device_sn_[32]              = { 0 };

////////////////////////////////////////////////////////////////////////////////

static void releaseParams()
{
    RELEASE_AA( optpar_me );
    RELEASE_AA( optpar_dev_uid );
    RELEASE_AA( optpar_dev_sn );
    RELEASE_AA( optpar_dev_usbpid ); 
    RELEASE_AA( optpar_fwfile );
}

static void showHelp()
{
    printf( "\n" );
    printf( " usuage : %s [option] (parameter) [firmware file]\n", optpar_me );
    printf( "\n" );
    printf( "\t -h, --help        : show help (this).\n" );
    printf( "\t -l, --list        : enumerate detected devices.\n" );
    printf( "\t -d, --devid (uid) : select uid device only.\n" );
    printf( "\t -s, --devsn (sn)  : select device only for sn.\n" );
    printf( "\t -p, --pid (pid)   : select USB PID (VID 2BC5 is fixed).\n" );
    printf( "\t                     e.g. 0407 == Mini S.\n" );
    printf( "\t                     e.g. 065B == Mini S.\n" );
    printf( "\t -a, --all         : select all devices.\n" );
    printf( "\t -e, --lessverbose : make verbose lesser.\n" );
    printf( "\t -v, --version     : shows versions only.\n" );
    printf( "\n" );
}

static void prtDevInfo( ob_device* device )
{
    if ( device != nullptr )
    {       
        ob_error *error = NULL;

        ob_device_info *dev_info =\
            ob_device_get_device_info(device, &error);

        if ( dev_info != nullptr )
        {
            const char *name = ob_device_info_name(dev_info, &error);
            uint16_t pid = ob_device_info_pid(dev_info, &error);
            uint16_t vid = ob_device_info_vid(dev_info, &error);
            const char *uid = ob_device_info_uid(dev_info, &error);
            const char *fw_ver = ob_device_info_firmware_version(dev_info, &error);
            const char *sn = ob_device_info_serial_number(dev_info, &error);
        
            printf( "%s, USB=%04X:%04X:%s, SN=%s, Ver. = %s\n",
                    name, vid, pid, uid, sn, fw_ver );

            ob_delete_device_info(dev_info, &error);
        }
    }
}

void dev_changed_cb(ob_device_list* rmd, ob_device_list* ads, void *ud) 
{
    ob_error *error = NULL;

    if( is_wait_reboot_complete_ == true )
    {
        if( ads != nullptr ) 
        {
            size_t device_count = \
                ob_device_list_device_count(ads, &error);

            if( device_count > 0 )  
            {
                // Orbbec SDK only acquire first device only .. 
                // is it right ?
                ob_device* device = \
                    ob_device_list_get_device( ads, 0, &error );

                ob_device_info* dev_info = \
                    ob_device_get_device_info( device, &error );

                bool is_asd = false;
                const char *sn = \
                    ob_device_info_serial_number( dev_info, &error );

                if( 0 == strcmp(sn, device_sn_) ) 
                {
                    rebooted_device_  = device;
                    is_asd            = true;
                    is_wait_reboot_complete_ = false;

                    unique_lock<mutex> lk(wait_reboot_mutex_);
                    wait_reboot_condition_.notify_all();
                }

                ob_delete_device_info( dev_info, &error );

                if( is_asd == false )
                {
                    ob_delete_device(device, &error);
                }
            }
        }

        if( rmd != nullptr )
        {
            size_t device_count = \
                ob_device_list_device_count( rmd, &error );

            if( device_count > 0 ) 
            {
                const char *uid = \
                    ob_device_list_get_device_uid( rmd , 0, &error );

                if( 0 == strcmp(device_uid_, uid) ) 
                {
                    is_device_removed_ = true;
                }
            }
        }
    }

    ob_delete_device_list( rmd, &error );
    ob_delete_device_list( ads, &error );
}

static void showDevList( ob_device_list *dl )
{
    if ( dl == nullptr ) return;

    ob_error *error = NULL;
    
    size_t devCnt = ob_device_list_device_count( dl, &error);
    
    for( size_t cnt=0; cnt<devCnt; cnt++ )
    {
        ob_device *dev = ob_device_list_get_device( dl, 0, &error );                
        printf( "[%3zu] ", cnt ); 
        prtDevInfo( dev );
    }
}

void dev_upgrade_cb( ob_upgrade_state st, const char *msg, uint8_t perc, void *ud) 
{
    printf( "\r .. upgrading %3u %% ", perc );

    if ( strlen( msg ) > 0 )
        printf( ", %s" );

    if( st == STAT_DONE ) 
    {
        is_upgrade_success_ = true;
        printf( "\n .. completed !\n" );
    }
}

bool upgrade_firmware( ob_device *dev, const char *fpath )
{
    const char *index     = strstr(fpath, ".img");
    bool        isImgFile = (bool)index;
    index                 = strstr(fpath, ".bin");
    bool isBinFile        = (bool)index;

    if( !(isImgFile || isBinFile) )
    {
        printf("Error, Invalid firmware file: %s\n", fpath);
        return false;
    }
    
    is_upgrade_success_ = false;
    ob_error *error     = NULL;
    ob_device_upgrade( dev, fpath, dev_upgrade_cb, false, &cb_ud, &error);

    return is_upgrade_success_;
}

int main(int argc, char **argv) 
{
    // Windows need sepearate paths ...
    char* argv0 = strdup( argv[0] );
    char* stk = strtok( argv0, "\\" );
    char* lstk = nullptr;
    while( stk != nullptr )
    {
        lstk = stk;
        stk = strtok( nullptr, "\\" );
    }

    if ( lstk != nullptr )
        optpar_me = strdup( lstk );

    RELEASE_A( stk );
    RELEASE_A( argv0 );

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

                case 'p':
                {
                    if ( optarg != nullptr )
                    {
                        optpar_dev_usbpid = strdup( optarg );
                        optpar_all = 0;
                        optpar_devonly = 1;
                        optpar_devtype = 2;
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
                "sdk.version=%u.%u.%u.%u\n",
                APP_V_MAJ, APP_V_MIN, APP_V_PAT, APP_V_BLD,
                ob_get_major_version(),
                ob_get_minor_version(),
                ob_get_patch_version(),
                ob_get_stage_version() );
        releaseParams();
        return 0;
    }

    if ( optpar_lessverbose == 0 )
    {
        printf( "AstraFWU-Win64, version %u.%u.%u.%u, %s\n",
                APP_V_MAJ, APP_V_MIN, APP_V_PAT, APP_V_BLD,
                "(C)2024 Raph.K@BearRobotics." );
    }

    if ( par_parsed == 0 )
    {
        showHelp();
        releaseParams();
        return 0;
    }

    // suppress messy logs ...
    // Context::setLoggerSeverity( OBLogSeverity::OB_LOG_SEVERITY_NONE );

    ob_error   *error = NULL;
    ob_context *ctx   = ob_create_context(&error);

    ob_set_device_changed_callback(ctx, dev_changed_cb, &cb_ud, &error);
    ob_device_list *dev_list = ob_query_device_list(ctx, &error);
    
    int devCnt = ob_device_list_device_count(dev_list, &error);
    
    if(devCnt == 0) 
    {
        printf("Device not found!\n");
        releaseParams();
        return 0;
    }

    // list option ?
    if ( optpar_list > 0 )
    {
        showDevList( dev_list );
        releaseParams();
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
            ob_device *dev = ob_device_list_get_device( dev_list, cnt, &error );    
            ob_device_info *dev_info = ob_device_get_device_info( dev, &error);
            
            if ( dev_info != nullptr )
            {
                const char *name = ob_device_info_name(dev_info, &error);
                const char *uid = ob_device_info_uid(dev_info, &error);
                const char *fw_ver = ob_device_info_firmware_version(dev_info, &error);
                const char *sn = ob_device_info_serial_number(dev_info, &error);
                char usb_pid[6] = {0};
                snprintf( usb_pid, 6, 
                          "%X", ob_device_info_pid(dev_info, &error) );
                
                switch( optpar_devtype )
                {
                    case 0:
                        if ( strcmp( uid, optpar_dev_uid ) == 0 )
                        {
                            fw_dev_lists.push_back( cnt );
                        }
                        break;

                    case 1:
                        if ( strcmp( sn,optpar_dev_sn )== 0 )
                        {
                            fw_dev_lists.push_back( cnt );
                        }
                        break;

                    case 2:
                        if ( strcmp( optpar_dev_usbpid, usb_pid ) == 0 )
                        {
                            fw_dev_lists.push_back( cnt );
                        }
                        break;
                }
            } /// of switch() ...
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
        ob_device *dev = \
            ob_device_list_get_device( dev_list, fw_dev_lists[cnt], &error );  
        printf( "Starting FW update : " );
        prtDevInfo(dev);

        char s_vid[32] = {0};
        char s_pid[32] = {0};

        // Store uid to wait device reboot
        ob_device_info *dev_info = \
            ob_device_get_device_info( dev, &error);

        snprintf( device_uid_, 128, "%s", 
                  ob_device_info_uid(dev_info, &error) );
        snprintf( device_sn_, 32, "%s", 
                  ob_device_info_serial_number(dev_info, &error) );

        ob_delete_device_info(dev_info, &error);

        if( upgrade_firmware(dev, optpar_fwfile) == false )
        {
            fprintf( stderr, "firmware upgrading failure.\n" );
            releaseParams();
            return -1;
        }

        printf( "Rebooting device .." );

        ob_device_reboot(dev, &error);
        ob_delete_device(dev, &error);
        ob_delete_device_list(dev_list, &error);

        // wait reboot complete
        printf("reboot completed\n");
        {
            unique_lock<mutex> lk(wait_reboot_mutex_);

            // Interesting, OrbbecSDK using lambda in C ... 
            wait_reboot_condition_.wait_for( lk, 
                chrono::milliseconds(60000), 
                []() { return !is_wait_reboot_complete_; }
            );
        }
        
        // Check is reboot complete
        if(rebooted_device_) 
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
