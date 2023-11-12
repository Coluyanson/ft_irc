#include "Server.hpp"
#include "Server.hpp"

Server::Server(const std::string &port, const std::string &psw) : _port(portConverter(port)), _psw(psw), _isPassword(psw.compare("") != 0)
{
	_commands["JOIN"] = Command::join;
	_commands["PRIVMSG"] = Command::privmsg;
	_commands["PING"] = Command::ping;
	_commands["KICK"] = Command::kick;
	_commands["INVITE"] = Command::invite;
	_commands["TOPIC"] = Command::topic;
	_commands["MODE"] = Command::mode;
	_commands["NICK"] = Command::nick;
	_commands["PASS"] = Command::pass;
	_commands["USER"] = Command::user;
	_commands["STATUS"] = Command::status;
	_commands["PART"] = Command::part;
	// _commands["WHO"] = Command::who;
	// _commands["USERHOST"] = Command::userhost;
}

Server::~Server()
{
}

const std::string	&Server::getPassword() const { return _psw; }


Client *Server::getClient(const std::string &clName)
{
	Client	*c = NULL;
	std::map<std::string, Client*>::const_iterator	it = _clients.find(clName);

	if (it != _clients.end())
		c = it->second;
	return(c);
}

void Server::updateNick(Client &client, const std::string &newName)
{
	std::string	oldName = client.getNickname();
	// Set nick in client object
	client.setNikcname(newName);
	if (client.getIsRegistered())
	{
		//Update nick in server
		_clients.erase(oldName);
		_clients[newName] = &client;
		//Update all channels




		//Update nick in all channels
		std::vector<Channel *> joinedChannels = client.getJoinedChannels();
		for (size_t i = 0; i < joinedChannels.size(); ++i)
		{
			joinedChannels[i]->updateNickInChannel(oldName, newName);
		}

		//Send update to all clients
		std::string	NEW_NICK = ":" + oldName + "!" + client.getUser() + "@localhost NICK " + newName + "\r\n";
		this->sendToAllClients(NEW_NICK);
	}
}

Client	*Server::getClientByFd(int fd) const
{
	std::map<std::string, Client*>::const_iterator	it = _clients.begin();
	std::map<std::string, Client*>::const_iterator	end = _clients.end();

	while (it != end)
	{
		if (it->second && it->second->getFd() == fd)
			return(it->second);
		++it;
	}

	std::list<Client*>::const_iterator	lit = _clientsNotRegistered.begin();
	std::list<Client*>::const_iterator	lend = _clientsNotRegistered.end();

	while (lit != lend)
	{
		if (*lit && (*lit)->getFd() == fd)
			return (*lit);
		++lit;
	}
	return (NULL);
}

Channel *Server::getChannel(const std::string &chName)
{
	std::string toLower = toLowerString(chName);
	std::map<std::string, Channel*>::iterator	it;

	if ((it = _channels.find(toLower)) != _channels.end())
		return(_channels[toLower]);
	return NULL;
}

void	Server::addChannel(Channel *ch)
{
	if (!ch || getChannel(ch->getName()))
		return;
	_channels[ch->getName()] = ch;
}

void	Server::deleteClient(Client *client)
{
	if (!client)
		return;
	if (client->getIsRegistered())
		client->deleteFromChannels(*this);
	else
		_clientsNotRegistered.remove(client);
	std::string		clientName = client->getNickname();
	_clients.erase(clientName);
	delete(client);
}

void	Server::deleteChannel(const std::string &chName)
{
	std::map<std::string, Channel *>::iterator	it = _channels.find(toLowerString(chName));

	if (it != _channels.end())
	{
		delete(it->second);
		_channels.erase(it);
	}
}

void	Server::run()
{
	signal(SIGINT, sigHandler);
	int serverSocket, newSocket;
	struct sockaddr_in serverAddr, newAddr;
	socklen_t addrSize;
	char buffer[512];

	memset(&serverAddr, 0, sizeof(sockaddr_in));
	memset(&newAddr, 0, sizeof(sockaddr_in));
	memset(&addrSize, 0, sizeof(socklen_t));
	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1)
		perror("socket");
	int opt = 1;
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
		perror("setsockopt");
	if (fcntl(serverSocket, F_SETFL, O_NONBLOCK) == -1)
		throw (std::runtime_error("fcntl-server"));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(this->_port);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)))
		perror("bind");
	if (listen(serverSocket, MAX_QUEUE_CONN) == -1)
		perror("listen");

	std::cout << "Server is listening on port " << this->_port << "..." << std::endl;

	int epoll_fd = epoll_create1(0);

	struct epoll_event event;
	memset(&event, 0, sizeof(epoll_event));
	event.events = EPOLLIN; // Monitor read events
	event.data.fd = serverSocket;

	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serverSocket, &event); // Add the server socket to the epoll

	struct epoll_event arrEvent[MAX_CLIENT]; // Create an event array to store events

	while (running)
	{
		int ready_fds = epoll_wait(epoll_fd, arrEvent, MAX_CLIENT, -1); // Wait for events

		if (ready_fds == -1)
			perror("epoll_wait");

		for (int i = 0; i < ready_fds; ++i)
		{
			if (arrEvent[i].data.fd == serverSocket)
			{
				newSocket = accept(serverSocket, (struct sockaddr*)&newAddr, &addrSize);
				if (newSocket == -1)
					perror("accept");
				if (fcntl(newSocket, F_SETFL, O_NONBLOCK) == -1)
					throw (std::runtime_error("fcntl-client"));
				event.data.fd = newSocket;
				event.events = EPOLLIN; // Monitor read events for the new socket
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, newSocket, &event);
				std::cout << "Connection established with a client." << std::endl;
				// Enter new fd in the list of not registered client
				Client * cl = new Client(newSocket);
				if (cl)
				{
					std::string	RPL_INFO = ":ircserv INFO :Connected to 42IRC server!\r\n";
					send(newSocket, RPL_INFO.c_str(), RPL_INFO.size(), 0);
					_clientsNotRegistered.push_back(cl);
				}
			}
			else
			{
				Client * c = NULL;
				int clientSocket = arrEvent[i].data.fd;
				c = getClientByFd(clientSocket);
				memset(buffer, 0, sizeof(buffer));
				int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
				std::cout << "Client: " << clientSocket << std::endl;
				if (bytesReceived <= 0)
				{
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, clientSocket, NULL);
					close(clientSocket);
					deleteClient(c);
					// Send disconnection to client?
					std::cout << "Client disconnected." << std::endl;
				}
				else if (c)
					msgAnalyzer(*c, buffer);
			}
		}
	}

	std::cout << "Sono uscito dal run" << std::endl;

	// DESTRUCTOR CALL
	for(std::list<Client*>::iterator it = _clientsNotRegistered.begin(); it != _clientsNotRegistered.end(); ++it)
	{
		send((*it)->getFd(), "QUIT :Server disconnected!\r\n", 29, 0);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, (*it)->getFd(), NULL);
		close((*it)->getFd());
		delete (*it);
	}
	for(std::map<std::string, Client *>::iterator it = _clients.begin(); it != _clients.end(); it++)
	{
		std::cout << "Closing fd " << it->second->getFd() << std::endl;
		send(it->second->getFd(), "QUIT :Server disconnected!\r\n", 29, 0);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, it->second->getFd(), NULL);
		close(it->second->getFd());
		delete it->second;
	}
	for(std::map<std::string, Channel *>::iterator it = _channels.begin(); it != _channels.end(); it++)
		delete it->second;
	
	close(epoll_fd);
	close(serverSocket);
}

void	Server::msgAnalyzer(Client &client, const char *message)
{
	std::string msg = message;
	size_t		pos;

	msg = client.getBuffer() + msg;
	client.setBuffer("");

	while ((pos = msg.find('\n')) != std::string::npos)
	{
		std::string			line;
		std::istringstream	iss(msg);

		std::getline(iss, line);
		msg.erase(0, pos + 1);
		if (client.getIsRegistered())
			cmdAnalyzer(client, line);
		else
			registration(client, line);
	}
	client.setBuffer(msg);
}

void	Server::welcomeMessage(Client &client)
{
	std::string serverName = ":ircserv";
	const int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	std::string RPL_WELCOME = serverName + " 001 " + client.getNickname() + " :Welcome to the 42 Internet Relay Network " + client.getNickname() + "\r\n";
	std::string RPL_YOURHOST = serverName + " 002 " + client.getNickname() + " :Hosted by Ale, Dami, Manu\r\n";
	std::string RPL_CREATED = serverName + " 003 " + client.getNickname() + " :This server was created in Nidavellir\r\n";

	send(client.getFd(), RPL_WELCOME.c_str(), RPL_WELCOME.length(), flags);
	send(client.getFd(), RPL_YOURHOST.c_str(), RPL_YOURHOST.length(), flags);
	send(client.getFd(), RPL_CREATED.c_str(), RPL_CREATED.length(), flags);
}

void	Server::registration(Client &client, const std::string &msg)
{
	std::cout << "\033[32m" << msg << "\033[0m" << std::endl;
	
	std::vector<std::string>	params;
	std::string					cmd;

	params = ft_split(msg, ' ');

	// Controllo empty command
	if (!params.size())
		return;

	cmd = params[0];
	params.erase(params.begin());

	if (!_isPassword)
		client.setPassTaken(true);

	//Unregognised command?

	if (!client.getPassTaken() && !cmd.compare("PASS"))
		Command::pass(*this, client, params);
	else if (!cmd.compare("NICK"))
		Command::nick(*this, client, params);
	else if (!cmd.compare("USER"))
		Command::user(*this, client, params);
	else
	{
		std::string error = "451 " + client.getNickname() + " :You have not registered\r\n";
		send(client.getFd(), error.c_str(), error.size(), 0);
		return;
	}
	if (!client.getNickname().empty() && !client.getUser().empty() && client.getPassTaken())
	{
		if (this->getClient(client.getNickname()))
		{
			std::string	ERR_NICKNAMEINUSE = "433 " + client.getNickname() + " :Nickname is already in use\r\n";
			send(client.getFd(), ERR_NICKNAMEINUSE.c_str(), ERR_NICKNAMEINUSE.size(), 0);
			client.setNikcname("");
			return;
		}
		std::cout << client.getNickname() << " registered!" << std::endl;
		client.setIsRegistered(true);
		welcomeMessage(client);
		_clientsNotRegistered.remove(&client);
		_clients[client.getNickname()] = &client;
	}
}

static void	fillParam(std::vector<std::string> &vParam, std::istringstream &iss)
{
	std::string	param, last;


	while (std::getline(iss, param, ' '))
	{
		if (param.empty())
			continue;
		else if (param[0] == ':')
		{
			std::getline(iss, last, (char)EOF);
			param.erase(0, 1);
			if (last.size() + param.size())
			{
				if (!last.empty())
					vParam.push_back(param + " " + last);
				else
					vParam.push_back(param);
			}
		}
		else
			vParam.push_back(param);
	}
}

void	Server::cmdAnalyzer(Client &client, const std::string &msg)
{
	// pulire stringa /r/n
	std::vector<std::string>						vParam;
	std::string										cmd;
	std::istringstream								iss(msg);
	std::map<std::string, commandFunct>::iterator	it;

	std::cout << "\033[32m" << msg << "\033[0m" << std::endl;
	iss >> cmd;
	if ((it = _commands.find(cmd)) != _commands.end())
	{
		fillParam(vParam, iss);
		_commands[cmd](*this, client, vParam);
	}
	else
	{
		std::cout << "Unrecognized command" << std::endl;
	}
}

void Server::sendToAllClients(const std::string &msg)
{
	for (std::map<std::string, Client *>::iterator	it = _clients.begin(); it != _clients.end(); ++it)
	{
		if (it->second)
			send(it->second->getFd(), msg.c_str(), msg.size(), 0);
	}
}

void	Server::status()
{
	if (!_clientsNotRegistered.size())
		std::cout << "LIST NOT REGISTERED" << std::endl;

	for (std::list<Client *>::iterator it = _clientsNotRegistered.begin(); it != _clientsNotRegistered.end(); ++it)
	{
		std::cout << "FD: " << (*it)->getFd() << std::endl;
	}

	std::cout << "CLIENTS: " << std::endl;

	for (std::map<std::string, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it )
		std::cout << "-" << it->second->getNickname() << std::endl;

	std::cout << "CHANNELS: " << std::endl;

	for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it )
	{
		std::cout << it->second->getName() << std::endl;
		std::vector<Client *> all = it->second->getAllClients();
		for (size_t i = 0; i < all.size(); ++i)
		{
			if (it->second->isOperator(all[i]->getNickname()))
				std::cout << "	-@" << all[i]->getNickname() << std::endl;
			else
				std::cout << "	-" << all[i]->getNickname() << std::endl;
		}
	}
}
