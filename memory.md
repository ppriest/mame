# Generalities

The main function of the memory subsystem is to go from an address to a read or write access as fast as possible.  In addition, caching is a requirement for at least instruction fetching.  But in addition per-access point caching would be nice for the drc, and in general small software tlbs (e.g. 4-slot caches or so) would be good in the cpus and wherever else is useful.

In addition, the Mame memory subsystem has an interesting and useful peculiarity: all accesses are databus sized and aligned.

What current cpus are damn good at is calling virtual methods in objects, especially single-inheritance objects.  So the idea is for the address space root to be an object which recursively calls the same method on a sub-object depending on part of the bits of the address, etc until a terminal object is reached that does the access.  A level of dispatching is just retrieving an object pointer in an array depending on the address (e.g. masking and shifting) and calling a fixed virtual method on it.  And, if we follow the current structure, we're talking one level of dispatching when the address bus is 14 bits or less and two otherwise.

If we're good with templates, we can have zero non-fixed computations on the path.  So it's mask with a constant, shift, read pointer, call method, do that again, do the access.  We'll have to measure whether caching is useful at that point.

Another very interesting property of that structure is that we can insert special objects anywhere in the tree without breaking the fundamentals or slowing down anything not using them.  I'm thinking in particular about:
* Banking objects, which are dispatch tables where the table pointer can be changed externally.  Replaces bankdev with a much lower cost.
* Triggers, object in the path between the dispatch and the access, that do things.  Example of things they can do is testing for contention (and manipulating the calling cpu icount as needed) or bus delays, but also implementing watchpoints with minimal overhead.

In addition, terminal objects can be varied.  You can have:
* Run-of-the-mill handler-calling objects.
* Run-of-the-mill memory access objects.
* Memory access objects with datawidth adaptation.
* Handler calling with sub-unit resolution.
* Ports and other things of the kind, if any.


# The classes
## Generalities

* Sizes are represented as 0..3 for size 8*2^n, e.g. 0=8, 1=16, 2=32, 3=64.
* Template parameter `_width_` is the databus width.
* Template parameter `_ashift_` is the address bus shift, e.g. what the address counts (bytes, words, etc).
* The `_new` suffixes will be gone at some point (when the old classes are removed and the collisions gone).
* In the read/write handlers, the unmodified address is passed down.

## Traits - handler_entry_size
In the struct `handler_entry_size<_width_>`, three typedefs are defined:
* `UINTX` points to `UINT<size>`
* `READ` points to `read<size>_delegate`
* `WRITE` points to `write<size>_delegate`

## Root - handle_entry_new
The root of all handlers is not a template.  It includes:
* The address space, to avoid passing it down on each access
* Flags, to know efficiently whether it's a dispatch entry, a trigger entry...
* A reference counter, for auto-deletion when needed

## Generic read/write handlers - handler_entry_read_new, handler_entry_write_new
Inherit from handler_entry_new, templatized on `_width_` and
`_ashift`.  Define a pure virtual method `read` (respectively `write`) to
do an access.  Typedefs `UINTX` to `UINT<size>`.

All handlers should inherit from one of these, and implement `read`
(or `write`) appropriately.

## Terminal read/write handlers - handler_entry_read_terminal_new, handler_entry_write_terminal_new
Inherit from the generic handlers, add a base address and an address mask.

## Memory zone read/write handlers - handler_entry_read_memory_new, handler_entry_write_memory_new
Inherit from the terminal read/write handlers, add a base pointer of
the appropriate type.  Implement the read/write method.

## Standard delegate read/write handlers - handler_entry_read_single_new, handler_entry_write_single_new
Inherit from the terminal read/write handlers, add a delegate of
the appropriate type.  Implement the read/write method.

## Subunit read/write handlers - handler_entry_read_multiple_new, handler_entry_write_multiple_new
Inherit from the terminal read/write handlers, the delegates/data
needed for subunit access.  Building that information is not
implemented yet though.  Implement the read/write method.

## Dispatching read/write handlers - handler_entry_read_dispatch_new, handler_entry_write_dispatch_new
Inherit from the generic handlers, adds a templatization on the start
and end bit in the address to take into account (boundaries included).
Implement the read/write method.  Population method missing.


# The plan

* Build a first version of the handler class tree (in progress)
* Have others kick it
* Make it better
* Decide of the interaction with address_space
* Actually implement all the install_*
* Handle the sub-unit handlers
* Add the watchpoints, at that point we should have reached the previous capabilities
* Add the funky stuff
