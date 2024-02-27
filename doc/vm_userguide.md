# How to access VM cluster for experiments?

**WARN: Please do not use shared VM cluster for development** -- Sometimes the process lasts as a zombie, dominating all machine resources and affecting the other students' processes.

* Using GT VPN and accessing machines by ``ssh``
    - For VPN client, you should check these [link 1](https://vpn.gatech.edu/global-protect/portal/portal.esp), [link 2](https://vpn.gatech.edu/global-protect/getsoftwarepage.esp).
	- There are five of them with hostnames: ``advos-0[1-5].cc.gatech.edu``.
	- Username and password: your GT account and password.
	- Reference command: ``$ ssh gburdell3@advos-03.cc.gatech.edu`` for accessing the third machine.

* Accessing any machines as you want
	- The machines have independent storage: need to copy files across them if you try to use the other nodes.
	- Try not to crowd in certain node: using other nodes if you find too many people at current host.
		- Resource contention may let your experiments have terrible numbers.
		- Commands to check peers: ``who``, ``top`` .. etc.

* Don't leave zombie processes and large files at machines -- we regularly clean up the machines, but if you face the zombie process issue, please ping TAs to clean it.sss

