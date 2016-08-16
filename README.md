# dnc: demo version of nc(1)

dnc is a very simple version of the venerable nc(1) that's intended for demo
purposes by explaining precisely what it's doing and giving users a little more
control about how it operates.

## Synopsis

Listen for TCP connections on port 8080 on all local IP addresses:

    # dnc -l -p 8080

Establish a TCP connection to a server listening on 127.0.0.1 port 8080:

    # dnc 127.0.0.1 8080

## TODO

* basic server
* basic client
* allow setting TCP keep-alive options
* allow setting listen backlog
* allow disabling poll(2) -- need a REPL?
