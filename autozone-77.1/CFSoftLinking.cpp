/*
 * Copyright (c) 2005-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#include "CFSoftLinking.h"
#include <mach-o/dyld.h>
#include <pthread.h>


static void *getCoreFoundationFuncPtr(const char *inFuncName)
{
    static const struct mach_header *header = NSAddImage("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", NSADDIMAGE_OPTION_WITH_SEARCHING);
    NSSymbol symbol = NSLookupSymbolInImage(header, inFuncName, NSLOOKUPSYMBOLINIMAGE_OPTION_BIND);
    return (symbol != NULL) ? NSAddressOfSymbol(symbol) : NULL;
}

CFTypeRef CFRetain(CFTypeRef cf) {
    static CFTypeRef (*func) (CFTypeRef) = (CFTypeRef (*) (CFTypeRef))getCoreFoundationFuncPtr("_CFRetain");
    return func ? func(cf) : NULL;
}

void CFRelease(CFTypeRef cf) {
    static void (*func) (CFTypeRef) = (void (*) (CFTypeRef))getCoreFoundationFuncPtr("_CFRelease");
    if (func) func(cf);
}

CFStringRef CFCopyDescription(CFTypeRef cf) {
    static CFStringRef (*func) (CFTypeRef) = (CFStringRef (*) (CFTypeRef))getCoreFoundationFuncPtr("_CFCopyDescription");
    return func ? func(cf) : NULL;
}

CFMutableDataRef CFDataCreateMutable(CFAllocatorRef allocator, CFIndex capacity) {
    static CFMutableDataRef (*func) (CFAllocatorRef, CFIndex) = (CFMutableDataRef (*) (CFAllocatorRef, CFIndex))getCoreFoundationFuncPtr("_CFDataCreateMutable");
    return func ? func(allocator, capacity) : NULL;
}

void CFDataAppendBytes(CFMutableDataRef theData, const UInt8 *bytes, CFIndex length) {
    static void (*func) (CFMutableDataRef, const UInt8*, CFIndex) = (void (*) (CFMutableDataRef, const UInt8*, CFIndex))getCoreFoundationFuncPtr("_CFDataAppendBytes");
    if (func) func(theData, bytes, length);
}

const UInt8*  CFDataGetBytePtr(CFDataRef theData) {
    static UInt8* (*func) (CFDataRef) = (UInt8* (*) (CFDataRef))getCoreFoundationFuncPtr("_CFDataGetBytePtr");
    return func ? func(theData) : NULL;
}

CFIndex CFDataGetLength(CFDataRef theData) {
    static CFIndex (*func) (CFDataRef) = (CFIndex (*) (CFDataRef))getCoreFoundationFuncPtr("_CFDataGetLength");
    return func ? func(theData) : 0;
}

CFDataRef CFPropertyListCreateXMLData(CFAllocatorRef allocator, CFPropertyListRef propertyList) {
    static CFDataRef (*func) (CFAllocatorRef, CFPropertyListRef) = (CFDataRef (*) (CFAllocatorRef, CFPropertyListRef))getCoreFoundationFuncPtr("_CFPropertyListCreateXMLData");
    return func ? func(allocator, propertyList) : NULL;
}

CFMessagePortRef CFMessagePortCreateLocal(CFAllocatorRef allocator, CFStringRef name, CFMessagePortCallBack callout, CFMessagePortContext *context, Boolean *shouldFreeInfo) {
    static CFMessagePortRef (*func) (CFAllocatorRef, CFStringRef, CFMessagePortCallBack, CFMessagePortContext*, Boolean*) = (CFMessagePortRef (*) (CFAllocatorRef, CFStringRef, CFMessagePortCallBack, CFMessagePortContext*, Boolean*))getCoreFoundationFuncPtr("_CFMessagePortCreateLocal");
    return func ? func(allocator, name, callout, context, shouldFreeInfo) : NULL;
}

CFRunLoopSourceRef CFMessagePortCreateRunLoopSource(CFAllocatorRef allocator, CFMessagePortRef local, CFIndex order) {
    static CFRunLoopSourceRef (*func) (CFAllocatorRef, CFMessagePortRef, CFIndex) = (CFRunLoopSourceRef (*) (CFAllocatorRef, CFMessagePortRef, CFIndex))getCoreFoundationFuncPtr("_CFMessagePortCreateRunLoopSource");
    return func ? func(allocator, local, order) : NULL;
}

void CFRunLoopAddSource(CFRunLoopRef rl, CFRunLoopSourceRef source, CFStringRef mode) {
    static void (*func) (CFRunLoopRef, CFRunLoopSourceRef, CFStringRef) = (void (*) (CFRunLoopRef, CFRunLoopSourceRef, CFStringRef))getCoreFoundationFuncPtr("_CFRunLoopAddSource");
    if (func) func(rl, source, mode);
}

CFRunLoopRef CFRunLoopGetMain() {
    static CFRunLoopRef (*func) () = (CFRunLoopRef (*) ())getCoreFoundationFuncPtr("_CFRunLoopGetMain");
    return func ? func() : NULL;
}

CFRunLoopRef CFRunLoopGetCurrent() {
    static CFRunLoopRef (*func) () = (CFRunLoopRef (*) ())getCoreFoundationFuncPtr("_CFRunLoopGetCurrent");
    return func ? func() : NULL;
}

void CFRunLoopRun() {
    static void (*func) () = (void (*) ())getCoreFoundationFuncPtr("_CFRunLoopRun");
    if (func) func();
}

CFStringRef CFStringCreateWithCString(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding) {
    static CFStringRef (*func) (CFAllocatorRef, const char *, CFStringEncoding) = (CFStringRef (*) (CFAllocatorRef, const char *, CFStringEncoding))getCoreFoundationFuncPtr("_CFStringCreateWithCString");
    return func ? func(alloc, cStr, encoding) : NULL;
}

CFStringRef CFStringCreateWithFormat(CFAllocatorRef alloc, CFDictionaryRef formatOptions, CFStringRef format, ...) {
    static CFStringRef (*func) (CFAllocatorRef, CFDictionaryRef, CFStringRef, va_list) = (CFStringRef (*) (CFAllocatorRef, CFDictionaryRef, CFStringRef, va_list))getCoreFoundationFuncPtr("_CFStringCreateWithFormatAndArguments");
    va_list args;
    va_start(args, format);
    CFStringRef str = func ? func(alloc, formatOptions, format, args) : NULL;
    va_end(args);
    return str;
}

CFIndex CFStringGetLength(CFStringRef string) {
    static CFIndex (*func) (CFStringRef) = (CFIndex (*) (CFStringRef))getCoreFoundationFuncPtr("_CFStringGetLength");
    return func ? func(string) : 0;
}

void CFStringGetCharacters(CFStringRef string, CFRange range, UniChar *buffer) {
    static void (*func) (CFStringRef, CFRange, UniChar*) = (void (*) (CFStringRef, CFRange, UniChar*))getCoreFoundationFuncPtr("_CFStringGetCharacters");
    if (func) func(string, range, buffer);
}

Boolean CFStringGetCString(CFStringRef string, char *buffer, CFIndex bufferSize, CFStringEncoding encoding) {
    static Boolean (*func) (CFStringRef, char*, CFIndex, CFStringEncoding) = (Boolean (*) (CFStringRef, char*, CFIndex, CFStringEncoding))getCoreFoundationFuncPtr("_CFStringGetCString");
    return func ? func(string, buffer, bufferSize, encoding) : false;
}

const char *CFStringGetCStringPtr(CFStringRef string, CFStringEncoding encoding) {
    static const char* (*func) (CFStringRef, CFStringEncoding) = (const char* (*) (CFStringRef, CFStringEncoding))getCoreFoundationFuncPtr("_CFStringGetCStringPtr");
    return func ? func(string, encoding) : NULL;
}

CFIndex CFStringGetMaximumSizeForEncoding(CFIndex length, CFStringEncoding encoding) {
    static CFIndex (*func) (CFIndex, CFStringEncoding) = (CFIndex (*) (CFIndex, CFStringEncoding))getCoreFoundationFuncPtr("_CFStringGetMaximumSizeForEncoding");
    return func ? func(length, encoding) : 0;
}

CFStringRef CFXMLCreateStringByEscapingEntities(CFAllocatorRef allocator, CFStringRef string, CFDictionaryRef entitiesDictionary) {
    static CFStringRef (*func) (CFAllocatorRef, CFStringRef, CFDictionaryRef) = (CFStringRef (*) (CFAllocatorRef, CFStringRef, CFDictionaryRef))getCoreFoundationFuncPtr("_CFXMLCreateStringByEscapingEntities");
    return func ? func(allocator, string, entitiesDictionary) : NULL;
}

const CFAllocatorRef get_kCFAllocatorMallocZone() {
    static CFAllocatorRef* data = (CFAllocatorRef*)getCoreFoundationFuncPtr("_kCFAllocatorMallocZone");
    return (data ? *data : NULL);
}

const CFStringRef get_kCFRunLoopCommonModes() {
    static CFStringRef* data = (CFStringRef*)getCoreFoundationFuncPtr("_kCFRunLoopCommonModes");
    return (data ? *data : NULL);
}
