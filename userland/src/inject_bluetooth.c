/* iOS 16.5 (20F66) H4 initialization compatibility for QCA WCN6750.
 * Apple's init sequence treats the optional Write Default Erroneous Data
 * Reporting command as mandatory. Skip that step only when the controller's
 * actual Supported Commands mask says it is absent. No HCI reply is altered. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ucontext.h>

static uintptr_t daemon_base;
/* Async-signal-safe fault location: no unwinder, allocator, or device data. */
static void report_fault(int signal_number,siginfo_t *info,void *opaque)
{
    ucontext_t *context=opaque;
    uintptr_t values[4]={(uintptr_t)signal_number,(uintptr_t)info->si_addr,
        context->uc_mcontext->__ss.__pc-daemon_base,
        context->uc_mcontext->__ss.__lr-daemon_base};
    char line[160]="Hakios Bluetooth fault: signal/address/pc-offset/lr-offset";
    size_t length=strlen(line);
    const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<4;i++) {
        line[length++]=' ';
        for(int shift=(int)sizeof(uintptr_t)*8-4;shift>=0;shift-=4)
            line[length++]=digits[(values[i]>>shift)&15];
    }
    line[length++]='\n';
    (void)write(STDERR_FILENO,line,length);
    kill(getpid(),signal_number); /* SA_RESETHAND keeps the original fatal outcome. */
}
static void (*finish_init_step)(unsigned,const char *);
static void skip_optional_error_reporting(void)
{
    fprintf(stderr,"Hakios Bluetooth: skipping unsupported optional error reporting\n");
    finish_init_step(0,"Hakios optional erroneous-data reporting unavailable");
}
static unsigned (*send_hci)(unsigned,void (*)(unsigned),...);
static unsigned char initial_random_address[6];
static void initial_address_complete(unsigned status)
{
    fprintf(stderr,"Hakios Bluetooth: initial LE random address completion status=%u\n",status);
    finish_init_step(status,"Hakios initial LE random address");
}
static void prepare_legacy_address(void)
{
    /* Native address formatting accepts the six-byte address in display order.
     * Use a fresh static random address; never log its bytes. */
    arc4random_buf(initial_random_address,sizeof(initial_random_address));
    initial_random_address[0]=(initial_random_address[0]&0x3f)|0xc0;
    fprintf(stderr,"Hakios Bluetooth: programming initial LE random address in legacy mode\n");
    unsigned status=send_hci(0xa5,initial_address_complete,initial_random_address);
    if(status) initial_address_complete(status);
}
__attribute__((constructor)) static void initialize(void)
{
    io_service_t service=IOServiceGetMatchingService(0,IOServiceNameMatching("bluetooth"));
    if(!service) return;
    CFTypeRef mask=IORegistryEntryCreateCFProperty(service,CFSTR("HakiosBluetoothSupportedCommands"),kCFAllocatorDefault,0);
    IOObjectRelease(service);
    if(!mask) return;
    bool absent=CFGetTypeID(mask)==CFDataGetTypeID() && CFDataGetLength((CFDataRef)mask)==64 &&
        !(CFDataGetBytePtr((CFDataRef)mask)[18]&0x08);
    CFRelease(mask);
    if(!absent) return;
    const struct mach_header_64 *header=NULL;
    for(uint32_t i=0;i<_dyld_image_count();i++) {
        const struct mach_header *candidate=_dyld_get_image_header(i);
        const char *name=_dyld_get_image_name(i);
        if(candidate && candidate->filetype==MH_EXECUTE && name && !strcmp(name,"/usr/sbin/bluetoothd")) {
            header=(const struct mach_header_64 *)candidate;
            break;
        }
    }
    const uint8_t expected_uuid[16]={0x96,0x39,0x30,0xf1,0xd5,0x65,0x32,0xb5,0x91,0xd6,0x43,0xaf,0x71,0xe2,0xc8,0x5a};
    bool image_matches=false;
    if(!header || header->magic!=MH_MAGIC_64 || header->ncmds>1024) return;
    const uint8_t *commands=(const uint8_t *)(header+1),*end=commands+header->sizeofcmds;
    for(unsigned i=0;i<header->ncmds;i++) {
        if((size_t)(end-commands)<sizeof(struct load_command)) return;
        const struct load_command *command=(const void *)commands;
        if(command->cmdsize<sizeof(*command) || command->cmdsize>(size_t)(end-commands)) return;
        if(command->cmd==LC_UUID && command->cmdsize>=sizeof(struct uuid_command))
            {
            image_matches=!memcmp(((const struct uuid_command *)command)->uuid,expected_uuid,sizeof(expected_uuid));
        }
        commands+=command->cmdsize;
    }
    if(!image_matches) { fprintf(stderr,"Hakios Bluetooth: unsupported daemon UUID; optional init unchanged\n"); return; }
    uintptr_t base=(uintptr_t)header;
    daemon_base=base;
    struct sigaction fault_action={0};
    fault_action.sa_sigaction=report_fault;
    fault_action.sa_flags=SA_SIGINFO|SA_RESETHAND;
    sigemptyset(&fault_action.sa_mask);
    sigaction(SIGSEGV,&fault_action,NULL);
    sigaction(SIGBUS,&fault_action,NULL);
    /* The native feature initializer populates Broadcom/Apple VSC flags from
     * Apple's model-derived chipset ranges. QCA implements neither range.
     * Preserve its zeroing, identity and logging; skip both range bodies. */
    uint32_t *bcm_range=(void *)(base+0x282ff4);
    uint32_t *apple_range=(void *)(base+0x28311c);
    if(*bcm_range!=0x51000689 || *apple_range!=0x511f4288) return;
    vm_address_t first=(vm_address_t)bcm_range&~((vm_address_t)vm_page_size-1);
    vm_address_t last=((vm_address_t)apple_range+vm_page_size)&~((vm_address_t)vm_page_size-1);
    if(vm_protect(mach_task_self(),first,last-first,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) {
        fprintf(stderr,"Hakios Bluetooth: cannot disable foreign vendor capabilities\n");
        return;
    }
    *bcm_range=0x12800009; /* mov w9, #-1: outside Broadcom range */
    *apple_range=0x12800008; /* mov w8, #-1: outside Apple range */
    sys_icache_invalidate((void *)first,last-first);
    if(vm_protect(mach_task_self(),first,last-first,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
        fprintf(stderr,"Hakios Bluetooth: cannot restore feature initializer protection\n");
        _exit(1);
    }
    fprintf(stderr,"Hakios Bluetooth: disabled Apple/Broadcom vendor capabilities\n");
    /* Allow LE Set Random Address (ID 0xa5) while the native init state is
     * active. It is now a real init step, completed by the controller. */
    uint32_t *init_mask=(void *)(base+0x10c968);
    if(*init_mask!=0xd28404e9) return;
    vm_address_t init_page=(vm_address_t)init_mask&~((vm_address_t)vm_page_size-1);
    if(vm_protect(mach_task_self(),init_page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) return;
    *init_mask=0xd28405e9;
    sys_icache_invalidate((void *)init_page,vm_page_size);
    if(vm_protect(mach_task_self(),init_page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) _exit(1);

    /* Native debug preferences select standard scanning, without synthesizing
     * successful responses to Apple's multi-core/vendor scan commands. */
    const void *low_keys[]={CFSTR("DisableLowPowerCoreScan")};
    const void *vsc_keys[]={CFSTR("DisableVSCScanParams")};
    const void *values[]={kCFBooleanTrue};
    CFDictionaryRef low=CFDictionaryCreate(NULL,low_keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef vsc=CFDictionaryCreate(NULL,vsc_keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(low && vsc) {
        CFPreferencesSetAppValue(CFSTR("LowPowerCoreScan"),low,CFSTR("com.apple.MobileBluetooth.debug"));
        CFPreferencesSetAppValue(CFSTR("VSCScanParams"),vsc,CFSTR("com.apple.MobileBluetooth.debug"));
        fprintf(stderr,"Hakios Bluetooth: selected native standard scan preferences\n");
    }
    if(low) CFRelease(low);
    if(vsc) CFRelease(vsc);
    void (**step)(void)=(void *)(base+0x7fbde0);
    const uint32_t *original=(const void *)(base+0x10b970);
    const uint32_t *completion=(const void *)(base+0x1092d8);
    if((uintptr_t)*step!=(uintptr_t)original || original[0]!=0xd100c3ff ||
        original[1]!=0xa9014ff4 || completion[0]!=0xd10103ff || completion[1]!=0xa90157f6) {
        fprintf(stderr,"Hakios Bluetooth: optional init ABI mismatch; unchanged\n"); return;
    }
    void (**periodic_step)(void)=(void *)(base+0x7fbdb0);
    if((uintptr_t)*periodic_step!=base+0x10b3bc || *(uint32_t *)(base+0x10b3bc)!=0xd100c3ff) return;
    vm_address_t page=(vm_address_t)step&~((vm_address_t)vm_page_size-1);
    if(vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) {
        fprintf(stderr,"Hakios Bluetooth: cannot update private init table\n"); return;
    }
    finish_init_step=(void *)(base+0x1092d8);
    *step=skip_optional_error_reporting;
    send_hci=(void *)(base+0x10c8c4);
    *periodic_step=prepare_legacy_address;
    vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_READ);
    fprintf(stderr,"Hakios Bluetooth: enabled QCA optional-command compatibility\n");
}
