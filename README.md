TOWARD MULTI-THREADED CONCURRENCY in HDF5
------------------------------------------

We propse to update HDF5 software to support multiple concurrent threads to read data stored in HDF5.

The lack of concurrent access is a long-standing limitation for the HDF5 library that creates deployment and adoption barriers for commercial multi-threaded applications. To date, there was no effort to overcome this restriction, mostly due to the difficulty of retrofitting a large code base with thread concurrency. 

Current multi-threaded applications use thread-safe builds of HDF5 to access data. Only one thread at a time is allowed into the library creating an I/O bottleneck. Another approach is to read HDF5 files directly by-passing the library. Due to the extent and intricacy of the HDF5 file format such applications usually support a limited set of HDF5 features and do not provide a general solution.

The recent HDF5 developments and HDF5 library architecture prompted us to propose a strategy for concurrent read access without API changes and relatively quick delivery of the desired feature when compared to a full HDF5 library rewrite.

The implementation strategy includes modifications to a few packages in the HDF5 library and development of a multi-threaded connector for reading data. All these improvements will be contributed to the Open Source HDF5 software.

Our approach is summarized as follows:

The HDF5 library is broken into packages within three layers.  The Virtual Object Layer (VOL) sits right under the public APIs. Its role is to redirect applications calls to the corresponding VOL connector(s) that can be external or native (HDF5 library proper).  The native connector consists of multiple packages that prepare I/O requests and pass them to the Virtual File Driver (VFD) layer for execution.  If all layers and packages could be made multi-threaded one would have a multi-threaded HDF5 library. Unfortunately, due to interdependencies among the packages, this is a daunting and disruptive task we would like to avoid.

However, if we add multi-threaded support to just six packages (H5VL, H5E, H5CX, H5P, H5I, H5S and H5FD), it would be possible to implement a “Bypass” VOL connector to support multiple concurrent threads in the HDF5 read calls. For these calls, the connector would bypass HDF5 library and perform multi-threaded reads using VFD layer.  Un-supported API calls would be routed to the HDF5 library and be handled as usual.  If desired, the connector could be extended in the future with more capabilities or can be deprecated when HDF5 becomes multi-threaded.  Since the connector is not part of the HDF5 library, its development will not create any technical debt. Similarly, if paired with suitable regression test code and documentation, integration of the retrofitted packages into the HDF5 library should reduce technical debt. This change will not affect existing applications. The sketch design is shown on the figure below.

<p align="center" width="100%"> 
  <img width="33%" src="https://user-images.githubusercontent.com/14047725/178508458-4e8491a9-6b80-4d63-a899-a3bef93cc84e.png"> 
</p>


