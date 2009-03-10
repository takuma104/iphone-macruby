========
AUTOZONE
========

:Copyright: 2008 Apple Inc. All rights reserved.

.. Contents::

Introduction
------------

**AutoZone** is a `scanning`_, `conservative`_, `generational`_, `multi-threaded`_ garbage collector.  Specifically, it is the garbage collector used by the Cocoa_ frameworks within `Mac OS X Leopard`_ and Xcode_, the premiere development environment for `Mac OS X`_ and iPhone_ application development, is one of several garbage collected applications that shipped with Leopard.

.. Note:: The implementation of the *AutoZone* (Mac OS X Objective-C Garbage Collector) will change significantly in Snow Leopard.

While **AutoZone** was tested and deployed with a focus on supporting Cocoa_ application development, the implementation is language agnostic.   For example, the MacRuby_ project uses the **AutoZone** collector to provide fully automatic garbage collection of object graphs that span between Ruby and Objective-C!

.. _Cocoa: http://developer.apple.com/Cocoa/
.. _Mac OS X Leopard: http://www.apple.com/macosx/
.. _MacRuby: http://www.macruby.org/trac/wiki/MacRuby
.. _Xcode: http://developer.apple.com/tools/xcode/
.. _iPhone: http://developer.apple.com/iphone/
.. _Mac OS X: http://developer.apple.com/macosx/

Implementation Overview
-----------------------

The **AutoZone** collector is implemented in C++ and is designed to work in a runtime where some or most of the application's memory may be managed by mechanisms other than the collector.  Object graphs may span between scanned and unscanned zones of memory.  Facilities are included for maintaining a reference count of any given memory object.

**AutoZone** also offers support for weak references that are automatically nullified when the memory referred to by the weak reference no longer has strong references.   For compiled languages, support for memory barriers can be integrated into the compiler such that normal assignments will update internal collector state.

The collector also includes statistics gathering and the ability to monitor the collector state from external processes.   Thus, the collector can dump information about the object graph within an application at any time.

The collector specifically does not support resurrection and will warn when resurrection is detected.

Some high level details of the implementation follow.  For specific details, see the source code.

Scanning
++++++++

The **AutoZone** collector actively scans memory, looking for references between objects in memory.   The collector builds a graph of these objects and any objects that are not rooted -- not connected directly or indirectly to globals, the stack, or have been manually retained -- are automatically finalized and deallocated.

Conservative
++++++++++++

Unlike some garbage collectors, **AutoZone** does not move memory.  As it is designed to work well within the C language and within runtimes that may be partially unscanned, the collector assumes that the address of a memory object may be meaningful.

Generational
++++++++++++

In general, most objects within an application are short lived.  Thus, the **AutoZone** collector uses a generational algorithm  such that short-lived objects are scanned more frequently than older objects.  By focusing on scanning the newer generation of objets more proactively, the collector can efficiently reap garbage -- no longer needed memory objects -- without incurring the cost of a full scan.

Multi-threaded
++++++++++++++

The **AutoZone** collector typically runs on its own background thread and will not block other threads of execution within an application.  The collector will rarely block the execution of other threads and, when it does so, minimizes the amount of time that execution is stopped.
