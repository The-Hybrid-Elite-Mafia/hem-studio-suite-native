#import <AppKit/AppKit.h>

// Public AppKit bootstrap, matching a normal JUCEApplication entry point.
void initialiseIntegrationApplication() {
    @autoreleasepool { [NSApplication sharedApplication]; }
}
