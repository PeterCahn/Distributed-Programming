# Client/Server Programming

Client and server communicates through an ASCII-based protocol to exchange files stored server side.

## Server

Concurrent server receives file transfer request and sends it to the querying client. Replies when server receives:
*	GET request
	* Positive answer
		* 1 byte: "+"
		* 2 bytes: "OK"
		* 4 bytes: number of bytes of requested file (in network byte order)
		* 4 bytes: last edit timestamp (in network byte order)
		* n bytes: file content
	
		Total: 11 bytes + n bytes
	* Negative answer (any error)
		* 1 byte: "-"
		* 3 bytes: "ERR"
		* 1 byte: CR
		* 1 byte: LF
* QUIT request
	Server closes connection with client.

## Client
Client connects to server and sends file transfer request.
*	GET request
	* 3 bytes: "GET" (1 per character)
	* 1 byte: blank space
	* n bytes: filename
	* 1 byte: CR
	* 1 byte: LF
	
	Total: 6 bytes + n bytes

* QUIT request
	* 4 bytes: "QUIT"
	* 1 byte: CR
	* 1 byte: LF
	
	Total: 6 bytes
