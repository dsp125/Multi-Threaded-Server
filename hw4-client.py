#!/usr/bin/env python3
"""
Python test client library for OpSys HW 4.
"""

import logging
import selectors
import socket
import threading
from typing import BinaryIO, Callable, List, Union, cast

BUF_SIZE = 1024
TIMEOUT = 5

# allow DEBUG events to be logged to the terminal
logging.basicConfig(level=logging.DEBUG)
# set default log level back to WARNING
logging.root.setLevel(logging.WARNING)

logger = logging.getLogger("clientlib")  # pylint: disable=invalid-name
# set log level for this module to INFO
logger.setLevel(logging.INFO)


class Connection:
    """
    A wrapper around a socket, for a unified interface between TCP and UDP.
    """

    def __init__(self, dest_port: int, dest_addr: str, sock_type: int):
        """
        Connect to a server at the given address and port, through TCP or UDP.
        """
        self.sock: socket.SocketType = socket.socket(socket.AF_INET, sock_type, 0)
        self.dest = (dest_addr, dest_port)
        if self.sock.type & socket.SOCK_STREAM:
            self.sock.connect(self.dest)
            self.type = socket.SOCK_STREAM
        elif self.sock.type & socket.SOCK_DGRAM:
            self.type = socket.SOCK_DGRAM
        else:
            self.close()
            raise ValueError("type must be socket.SOCK_STREAM or socket.SOCK_DGRAM")

    def __del__(self) -> None:
        if self.sock is not None:
            self.sock.shutdown(socket.SHUT_RDWR)
            self.sock.close()

    def close(self) -> None:
        """
        Shut down and close the underlying socket.
        """
        if self.sock is not None:
            self.sock.shutdown(socket.SHUT_RDWR)
            self.sock.close()
            self.sock = None  # type: ignore

    def fileno(self) -> int:
        """
        Return the integer file descriptor of the underlying socket.
        """
        return cast(int, self.sock.fileno())

    def send(self, data: bytes, flags: int = 0) -> int:
        """
        Send a raw message to the server.
        """
        if self.type == socket.SOCK_STREAM:
            self.sock.sendall(data, flags)
            return len(data)
        # else:
        return cast(int, self.sock.sendto(data, flags, self.dest))

    def recv(self, flags: int = 0) -> bytes:
        """
        Receive a raw message from the server.
        """
        if self.type == socket.SOCK_STREAM:
            data = self.sock.recv(BUF_SIZE, flags)
        else:
            addr = ("", 0)
            while addr != self.dest:
                data, addr = self.sock.recvfrom(BUF_SIZE, flags)
        return cast(bytes, data)


class TimeoutException(Exception):
    """
    Raised when a command times out on the server, or a race condition is hit.
    """

    pass


class InvalidResponse(Exception):
    """
    Raised when we get a bad response from the server.
    """

    pass


def _format_response(raw: bytes) -> str:
    """
    Cleans up a raw response so it can be returned to a user.
    """
    resp = raw.decode().strip("\n")
    if resp.startswith("ERROR "):
        return "{}: {}".format(*resp.split(" ", maxsplit=1))
    return resp


class Client:
    """
    :param handler:
        A function that is called for incoming messages.
        The arguments it receives are the message type (FROM or SHARE), the
        sender's user ID, and the message/file itself.
    :param server_port:
        The port the server is running on.
    :param server_addr:
        The IP address/hostname the server is running on.
    :param sock_type:
        The connection type to use: socket.SOCK_STREAM or socket.SOCK_DGRAM for
        TCP or UDP, respectively.
    """

    def __init__(
        self,
        handler: Callable[[str, str, bytes], None],
        server_port: int,
        server_addr: str = "127.0.0.1",
        sock_type: int = socket.SOCK_STREAM,
    ):
        self._conn = Connection(server_port, server_addr, sock_type)
        self._cond = threading.Condition()
        # whether the underlying connection has been closed
        self.closed = False
        # whether a command is waiting for a response
        self.waiting = False
        self._handler = handler
        self._thread = threading.Thread(target=self._socket_reader, daemon=True)
        self._thread.start()

    def close(self) -> None:
        """
        Close the connection to the server.
        """
        if not self.closed:
            self.closed = True
            self._conn.close()

    def _socket_reader(self) -> None:
        """
        Waits on the socket in a separate thread, handles messages from other
        clients and passes on responses from the server.
        """
        sel = selectors.DefaultSelector()
        sel.register(self._conn.sock, selectors.EVENT_READ)
        while True:
            events = sel.select()
            if self.closed:
                return
            logger.debug("socket_reader(): left select")
            if not events:
                # spurious wakeup? IDK if this can happen
                continue
            # get the condition variable
            with self._cond:
                logger.debug("socket_reader(): got lock")
                if self.waiting:
                    # a command is waiting for a reply, notify it
                    logger.debug("socket_reader(): letting main thread handle message")
                    self._cond.notify()
                    # wait for it to finish
                    self._cond.wait(TIMEOUT)
                    logger.debug("socket_reader(): done waiting")
                    continue
                # got a new message from the server, handle it
                logger.debug("socket_reader(): receiving data from server")
                data = self._conn.recv()
                if not data:
                    logger.debug("socket_reader(): server closed the connection")
                    # close our end
                    self.close()
                    break
                if not self._handle_incoming(data):
                    logger.error(
                        "socket_reader(): got invalid message with header %s",
                        repr(data.split(b" ", maxsplit=1)[0]),
                    )

    def _handle_incoming(self, data: bytes) -> bool:
        if self._conn.type == socket.SOCK_DGRAM:
            return False
        if data.startswith(b"FROM "):
            # handle incoming message
            parts = data.split(b" ", maxsplit=3)
            sender = parts[1].decode()
            length = int(parts[2])
            message = parts[3]
            # ensure the entire message is read
            while len(message) < length + 1:
                message += self._conn.recv()
            if message[-1] == ord("\n"):
                message = message[:-1]  # strip newline
            else:
                logger.warning("got incoming message without an ending newline")
            self._handler("FROM", sender, message)
            return True
        if data.startswith(b"SHARE "):
            # handle shared file
            header, *file_start = data.split(b"\n", maxsplit=1)
            parts = header.split(b" ", maxsplit=2)
            sender = parts[1].decode()
            length = int(parts[2])
            filedata = b"".join(file_start)
            while len(filedata) < length:
                chunk = self._conn.recv()
                if len(chunk) != min(1024, length - len(filedata)):
                    logger.warning("got invalid chunk while receiving shared file")
                filedata += chunk
            self._handler("SHARE", sender, filedata)
            return True
        return False

    def send_and_wait(self, data: bytes, timeout: float = TIMEOUT) -> bytes:
        """
        Send data to the server, wait for the socket_reader thread to tell us
        when a response comes in, then receive and return it.

        Raise TimeoutException if the socket_reader thread doesn't notify us
        within the specified timeout period.
        """
        with self._cond:
            logger.debug("send_and_wait(): got lock")
            self._conn.send(data)
            self.waiting = True
            logger.debug("send_and_wait(): waiting on condition variable")
            if not self._cond.wait(timeout=timeout):
                self.waiting = False
                raise TimeoutException
            logger.debug("send_and_wait(): got lock back")
            self.waiting = False
            logger.debug("send_and_wait(): receiving data")
            resp = self._conn.recv()
            # notify socket_reader that we're done reading
            self._cond.notify()
            return resp

    def login(self, userid: str) -> str:
        """
        Log into the server with the given userid.
        """
        request = b"LOGIN %b\n" % userid.encode()
        return _format_response(self.send_and_wait(request))

    def who(self) -> List[str]:
        """
        Get a list of users on the server.
        """
        lines = self.send_and_wait(b"WHO\n").decode().strip("\n").split("\n")
        if lines[0] != "OK!":
            raise InvalidResponse('expected "OK!", got "{}"'.format(lines[0]))
        return lines[1:]

    def logout(self) -> None:
        """
        Log out of the server.
        """
        resp = self.send_and_wait(b"LOGOUT\n")
        if resp != b"OK!\n":
            raise InvalidResponse('expected "OK!\\n", got "{}"'.format(resp.decode()))

    def send(self, recipient: str, message: bytes) -> str:
        """
        Send a private message to a logged-in user.
        """
        request = "SEND {:s} {:d}\n".format(recipient, len(message)).encode()
        request += message
        return _format_response(self.send_and_wait(request))

    def broadcast(self, message: bytes) -> str:
        """
        Send a message to all connected users on the server.
        """
        request = "BROADCAST {:d}\n".format(len(message)).encode() + message
        return _format_response(self.send_and_wait(request))

    def share(self, recipient: str, file: Union[BinaryIO, bytes]) -> str:
        """
        Share a file with another user.
        """
        if isinstance(file, bytes):
            filedata = file
        else:
            filedata = file.read()
        filelen = len(filedata)
        with self._cond:
            request = b"SHARE %b %d\n" % (recipient.encode(), filelen)
            self._conn.send(request)
            logger.debug("share(): sent header, waiting for OK")
            self.waiting = True
            if not self._cond.wait(timeout=TIMEOUT):
                self.waiting = False
                raise TimeoutException
            resp = self._conn.recv()
            logger.debug("share(): got something")
            # notify socket_reader that we're done reading
            self._cond.notify()
            if resp != b"OK!\n":
                self.waiting = False
                msg = resp.decode().strip("\n")
                return "{}: {}".format(*msg.split(" ", maxsplit=1))

            for i in range(0, filelen, 1024):
                self._conn.send(filedata[0 + i : 1024 + i])
                logger.debug("share(): sent chunk, waiting for OK")
                if not self._cond.wait(timeout=TIMEOUT):
                    self.waiting = False
                    raise TimeoutException
                resp = self._conn.recv()
                logger.debug("share(): got something")
                # notify socket_reader that we're done reading
                self._cond.notify()
                formatted = _format_response(resp)
                if resp != b"OK!\n":
                    self.waiting = False
                    return formatted
            self.waiting = False
            return _format_response(resp)