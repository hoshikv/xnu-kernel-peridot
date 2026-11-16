/* BlueTool's XPC boot contract for the already-bootstrapped Hakios QCA WCN6750.
 * No Broadcom scripts are executed. Unsupported requests return an error. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The public SDK omits XPC headers; these are the libxpc interfaces used here. */
typedef void *xpc_object_t;
typedef void *xpc_connection_t;
extern const char _xpc_type_dictionary, _xpc_type_connection;
extern const void *xpc_get_type(xpc_object_t);
extern xpc_connection_t xpc_connection_create_mach_service(const char *,dispatch_queue_t,uint64_t);
extern void xpc_connection_set_event_handler(xpc_connection_t,void (^)(xpc_object_t));
extern void xpc_connection_resume(xpc_connection_t);
extern void xpc_connection_send_message(xpc_connection_t,xpc_object_t);
extern uid_t xpc_connection_get_euid(xpc_connection_t);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t,const char *);
extern const char *xpc_dictionary_get_string(xpc_object_t,const char *);
extern void xpc_dictionary_set_int64(xpc_object_t,const char *,int64_t);
extern void xpc_release(xpc_object_t);
/* Darwin kernel-control ABI; also omitted from the public iOS SDK. */
struct bt_ctl_info { uint32_t ctl_id; char ctl_name[96]; };

static int controller_ready(void)
{
    io_service_t service=IOServiceGetMatchingService(0,IOServiceNameMatching("bluetooth"));
    if(!service) return ENODEV;
    CFTypeRef ready=IORegistryEntryCreateCFProperty(service,CFSTR("HakiosBluetoothReady"),kCFAllocatorDefault,0);
    IOObjectRelease(service);
    bool running=ready==kCFBooleanTrue;
    if(ready) CFRelease(ready);
    if(!running) return EIO;
    int fd=socket(PF_SYSTEM,SOCK_STREAM,2 /* SYSPROTO_CONTROL */);
    if(fd<0) return errno;
    struct bt_ctl_info info={0};
    strlcpy(info.ctl_name,"com.hakios.bluetooth",sizeof(info.ctl_name));
    int result=ioctl(fd,0xc0644e03UL /* CTLIOCGINFO */,&info)<0 ? errno : 0;
    close(fd);
    return result;
}
static void request(xpc_connection_t peer,xpc_object_t message)
{
    if(xpc_get_type(message)!=&_xpc_type_dictionary) return;
    xpc_object_t reply=xpc_dictionary_create_reply(message);
    if(!reply) return;
    const char *command=xpc_dictionary_get_string(message,"kMsgId");
    xpc_object_t arguments=xpc_dictionary_get_value(message,"kMsgArgs");
    const char *script=arguments && xpc_get_type(arguments)==&_xpc_type_dictionary ?
        xpc_dictionary_get_string(arguments,"script") : NULL;
    int result=ENOTSUP;
    /* Firmware and NVM initialization were both completed by XNU before launchd. */
    if(command && script && !strcmp(command,"RunBuiltinScript") &&
        (!strcmp(script,"boot") || !strcmp(script,"init")))
        result=controller_ready();
    fprintf(stderr,"Hakios BlueTool: command=%.48s script=%.48s result=%d\n",
        command?command:"(missing)",script?script:"(missing)",result);
    xpc_dictionary_set_int64(reply,"result",result);
    xpc_connection_send_message(peer,reply);
    xpc_release(reply);
}
int main(void)
{
    setbuf(stderr,NULL);
    xpc_connection_t listener=xpc_connection_create_mach_service("com.apple.BlueTool",dispatch_get_main_queue(),1);
    if(!listener) return 1;
    xpc_connection_set_event_handler(listener,^(xpc_object_t object) {
        if(xpc_get_type(object)!=&_xpc_type_connection) return;
        xpc_connection_t peer=object;
        uid_t uid=xpc_connection_get_euid(peer);
        if(uid!=0 && uid!=501) return;
        xpc_connection_set_event_handler(peer,^(xpc_object_t message) { request(peer,message); });
        xpc_connection_resume(peer);
    });
    xpc_connection_resume(listener);
    fprintf(stderr,"Hakios BlueTool: listening for the QCA bootstrap contract\n");
    dispatch_main();
}
