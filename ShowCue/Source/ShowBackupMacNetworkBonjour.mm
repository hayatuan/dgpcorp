#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

@interface ShowCueLanBonjourHelper : NSObject <NSNetServiceDelegate, NSNetServiceBrowserDelegate>
@property (nonatomic, strong) NSNetService* advertiser;
@property (nonatomic, strong) NSNetServiceBrowser* browser;
@end

@implementation ShowCueLanBonjourHelper

- (void) startAdvertiserWithPort:(int) port
{
    [self stopAdvertiser];

    NSString* hostName = [[NSHost currentHost] localizedName];

    if (hostName.length == 0)
        hostName = @"ShowCue";

    self.advertiser = [[NSNetService alloc] initWithDomain:@"local."
                                                      type:@"_showcue-sync._udp."
                                                      name:hostName
                                                      port:(int) port];
    self.advertiser.delegate = self;
    [self.advertiser publish];
}

- (void) stopAdvertiser
{
    if (self.advertiser != nil)
    {
        [self.advertiser stop];
        self.advertiser.delegate = nil;
        self.advertiser = nil;
    }
}

- (void) beginBrowseForPermission
{
    [self endBrowse];

    self.browser = [[NSNetServiceBrowser alloc] init];
    self.browser.delegate = self;
    [self.browser searchForServicesOfType:@"_showcue-sync._udp." inDomain:@"local."];
}

- (void) endBrowse
{
    if (self.browser != nil)
    {
        [self.browser stop];
        self.browser.delegate = nil;
        self.browser = nil;
    }
}

- (void) netServiceWillPublish:(NSNetService*) sender
{
    (void) sender;
}

- (void) netService:(NSNetService*) sender didNotPublish:(NSDictionary<NSString*, NSNumber*>*) errorDict
{
    (void) sender;
    (void) errorDict;
}

- (void) netServiceBrowser:(NSNetServiceBrowser*) browser
           didFindService:(NSNetService*) service
               moreComing:(BOOL) moreComing
{
    (void) browser;
    (void) service;
    (void) moreComing;
}

- (void) netServiceBrowser:(NSNetServiceBrowser*) browser didNotSearch:(NSDictionary<NSString*, NSNumber*>*) errorDict
{
    (void) browser;
    (void) errorDict;
}

@end

static ShowCueLanBonjourHelper* gShowCueBonjourHelper = nil;

static ShowCueLanBonjourHelper* getHelper()
{
    if (gShowCueBonjourHelper == nil)
        gShowCueBonjourHelper = [[ShowCueLanBonjourHelper alloc] init];

    return gShowCueBonjourHelper;
}

extern "C" void showcue_mac_open_local_network_settings (void)
{
    NSString* urlString = @"x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork";
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString: urlString]];
}

extern "C" void showcue_mac_request_local_network_prompt (void)
{
    [getHelper() beginBrowseForPermission];

    dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (1.2 * NSEC_PER_SEC)),
                    dispatch_get_main_queue(), ^
    {
        [getHelper() endBrowse];
    });
}

extern "C" void showcue_mac_start_bonjour_advertiser (int discoveryPort)
{
    [getHelper() startAdvertiserWithPort: discoveryPort];
}

extern "C" void showcue_mac_stop_bonjour_advertiser (void)
{
    [getHelper() stopAdvertiser];
    [getHelper() endBrowse];
}
