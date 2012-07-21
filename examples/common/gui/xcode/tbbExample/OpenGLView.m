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

#import "OpenGLView.h"
#import <OpenGL/gl.h>
#import "tbbAppDelegate.h"

int x,y;
void on_mouse_func(int x, int y, int k);
void on_key_func(int x);
void* windows_ptr=0;

//defined in cpp-file
extern char* window_title;
extern int cocoa_update;
extern int g_sizex, g_sizey;
extern unsigned int *g_pImg;

void draw(void)
{
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POINT);
    glDrawPixels(g_sizex, g_sizey, GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV, g_pImg);
    glEnd();
    glFlush();
}

@implementation OpenGLView

@synthesize timer;

- (void) drawRect:(NSRect)start
{
    x=g_sizex;
    y=g_sizey;
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    draw();
    glFlush();
    timer = [NSTimer scheduledTimerWithTimeInterval:0.03 target:self selector:@selector(update_window) userInfo:nil repeats:YES];
}

-(void) update_window{
    if( cocoa_update ) draw();
    if( window_title )[_window setTitle:[NSString stringWithFormat:@"%s", window_title]];
    return;
}

-(void) keyDown:(NSEvent *)theEvent{
    on_key_func([theEvent.characters characterAtIndex:0]);
    return;
}

-(void) mouseDown:(NSEvent *)theEvent{
    // mouse event for seismic and fractal
    NSPoint point= theEvent.locationInWindow;
    x = point.x;
    y = point.y;
    NSRect rect = self.visibleRect;
    on_mouse_func(x*g_sizex/rect.size.width,y*g_sizey/rect.size.height,1);
    draw();
    return;
    
}

- (BOOL) acceptsFirstResponder
{
    return YES;
}

- (void) rightMouseDown:(NSEvent *)theEvent
{
    return;
}

-(void) viewDidEndLiveResize
{
    NSRect rect = self.visibleRect;
    x=rect.size.width;
    y=rect.size.height;
    glPixelZoom((float)x/(float)g_sizex, (float)y/(float)g_sizey);
    [_window setTitle:[NSString stringWithFormat:@"X=%d Y=%d", x,y]];
    draw();
    return;
}

@end
