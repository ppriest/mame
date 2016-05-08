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


# The plan

* Build a first version of the handler class tree (in progress)
* Have others kick it
* Make it better
* Decide of the interaction with address_space
* Actually implement all the install_*
* Handle the sub-unit handlers
* Add the watchpoints, at that point we should have reached the previous capabilities
* Add the funky stuff
