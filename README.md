# tcp_packet_transfer

<pre>
./build.sh
</pre>

Then open two terminals. In the first terminal type (the port number is not important):

<pre>
./bin/server 15213
</pre>

In the second terminal type:

<pre>
./bin/client 127.0.0.1 15213
</pre>

This will start a simple demo that sends 16 packets from the "client" program to the "server" program over the localhost connection.





