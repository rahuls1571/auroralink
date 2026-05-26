# libEGD

libEGD is a library that allows users to communicate using the EGD protocol. The core library is implemented in `C++`,
but it also exports a `C` interface for `C` users, and to allow easy language bindings in the future.

## Building this repo

The suggested way to build this project is to build use Docker and GNU Make. This will automate the installation of
dependencies, and cut down on the amount of commands you need to run in order to build the repo. The make commands
available are documented below:

* `make build` - Builds the project using the CMakeLists.txt inside the docker container, and leaves the resulting
compiled code on the host file system for build checkpoints
* `make build-shell` - Opens a shell in the build container with the source directory mounted in place
* `make docs serve` - Generates documentation for the library, and serves the documentation up on `http://localhost:3002`
* `make dev_image` - Generates a development image with a debug version of the library installed
* `make image` - Generates a runtime image with a release version of the library installed
* `make clean` - Cleans the project, including compiled code, images, and build artifacts

This project also provides a [CMakeLists.txt](CMakeLists.txt) which can be used to build the code outside of docker if
you are willing to install the libraries dependencies which are installed in the [Dockerfile](hack/docker/Dockerfile.builder)

## Using the library

All the examples below will be compiled when you run `make` and can be run inside the docker container to test them out

### TL;DR

Read the code

### JSON

The library provides a `JsonClient`(C++) and `JsonClientHandle`(C) that allows users to send and receive EGD data, as 
JSON strings that get decoded/encoded into the proper payload by the library. This is the recommended way to use this
library, as this will cut down on the amount of decoding code you have to write, and will make it much easier to use
this library. Examples of using this client can be found below:

| Example                                               | Description                                            |
| ----------------------------------------------------- | ------------------------------------------------------ |
| [json_subscribe.cpp](src/examples/json_subscribe.cpp) | Subscribes to an EGD page using the JSON client in C++ |
| [json_publish.cpp](src/examples/json_publish.cpp)     | Publishes an EGD page using the JSON client in C++     |
| [json_subscribe.c](src/examples/json_subscribe.c)     | Subscribes to an EGD page using the JSON client in C   |
| [json_publish.c](src/examples/json_publish.c)         | Publishes an EGD page using the JSON client in C       |

#### Raw

The library also provides an `EgdClient`(C++) and `EgdClientHandle`(C) that allows users to send and receive raw EGD
data. Note that this is not the suggested way of subscribing to data, since it requires the user to write the encoding,
and decoding of the payload. This should only be done when every microsecond of execution time matters. Otherwise, you
should use the JSON method, which adds a small amount of execution time, but will make the library much easier to use.
Brief examples of using the raw client can be found below:

| Example                                                         | Description                                            |
| --------------------------------------------------------------- | ------------------------------------------------------ |
| [raw_bytes_subscribe.cpp](src/examples/raw_bytes_subscribe.cpp) | Subscribes to an EGD page using the base client in C++ |
| [raw_bytes_publish.cpp](src/examples/raw_bytes_publish.cpp)     | Publishes an EGD page using the base client in C++     |
| [raw_bytes_subscribe.c](src/examples/raw_bytes_subscribe.c)     | Subscribes to an EGD page using the base client in C   |
| [raw_bytes_publish.c](src/examples/raw_bytes_publish.c)         | Publishes an EGD page using the base client in C       |

## The EGD Spec

I am no expert in EGD, so this is my best attempt at describing what this library does in order to conform to what I
would consider the "EGD spec".

### Config

The [config](src/egd/config) directory deals with fetching and parsing config, so the user shouldn't need to know all of
these details, but here is a summary of what it does

#### Fetching

Configuration for the EGD protocol can be fetched using an HTTP GET to the path:

`http://<server_host>:<server_port>/EGD?Action=GetDoc&Type=<type>&Active=True&ProducerID=<producer_id>`

* `server_host` - The host name, or IP address of a ControlST or other compliant web server that will serve up this config
* `server_port` - The port that the config is served up from, defaults to `7938`
* `type` - The "type" of data, the only valid values here are `ConsumedData` and `ProducedData`. The library contains a
`ConsumerConfig` and `ProducerConfig` to allow a user to fetch either one. I have personally only ever used `ProducedData`
* `producer_id` - The identification number for the server that you are requesting. From what I can tell, this is the
64 bit representation of the IP address, and is the same as `inet_addr(server_host)` in C

#### Parsing

After sending a request to the URL above (assuming you requested `ProducedData`), the server will respond with an XML
document structured like the one below:
```xml
<?xml version="1.0" encoding="utf-8"?>
<ProducedData EGDSpecVersion="3.04" xmlns="http://geindustrial.com/EGD">
    <!--
    There can be multiple Producer sections in this config. Each one links to a ProducerId.
    We don't do anything with any attributes in this specific tag, but we do use the contents
    -->
	<Producer Name="A_SVR" ProducerId="12345678" Pvn="1" ConfigTimeSecs="1563558876" ConfigTimeNSecs="18340800">
	    <!--
	    Same as the producer section, there can be multiple Exchange sections in the config. We loop through each one,
	    and save it's information by the "Page" name so we can look it up later.
	    -->
		<Exchange ExchangeId="1" SigMajor="16" PeriodSecs="0" PeriodNSecs="10000000" ConfigTimeSecs="1563558874" ConfigTimeNSecs="0" DataLength="36" Page="DG_OUT">
		    <!--
		    Same as all other sections documented, there can be multiple Var section in each exchange. Each section should
		    not contain anything but attributes. Each Var in an exchange will be associated with that exchange
		    -->
			<Var Name="VAR_NAME" DType="REAL" Writable="true" VOffs="192" />
		</Exchange>
	</Producer>
</ProducedData>
```

### Client

The [client](src/egd/client) directory contains the implementations for actually communicating over EGD. The client
follows a publish-subscribe paradigm. The only methods used to communicate over EGD are `Publish` and `Subscribe`

* `Publish` - Forms the EGD header, given the page information read from the config, then sends the message out over UDP
to the requested IP address
* `Subscribe` - Adds a thread that will poll a socket, and when it receives the requested "page" it will call the callback
provided by the user with the message.
