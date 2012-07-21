/*
 Copyright 2005-2012 Intel Corporation.  All Rights Reserved.
 
 The source code contained or described herein and all documents related
 to the source code ("Material") are owned by Intel Corporation or its
 suppliers or licensors.  Title to the Material remains with Intel
 Corporation or its suppliers and licensors.  The Material is protected
 by worldwide copyright laws and treaty provisions.  No part of the
 Material may be used, copied, reproduced, modified, published, uploaded,
 posted, transmitted, distributed, or disclosed in any way without
 Intel's prior express written permission.
 
 No license under any patent, copyright, trade secret or other
 intellectual property right is granted to or conferred upon you by
 disclosure or delivery of the Materials, either expressly, by
 implication, inducement, estoppel or otherwise.  Any license under such
 intellectual property rights must be express and approved by Intel in
 writing.
 */

//
//  Created by Xcode* 4.3.2
//

#import "tbbAppDelegate.h"
#import <pthread.h>

@implementation tbbAppDelegate

@synthesize window = _window;

//declared in macvideo.cpp file
extern int g_sizex, g_sizey;

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Insert code here to initialize your application
    NSRect windowSize;
    windowSize.size.height = g_sizey;
    windowSize.size.width = g_sizex;
    windowSize.origin=_window.frame.origin;
    [_window setFrame:windowSize display:YES];

}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *) sender
{
    return YES;
}

@end
