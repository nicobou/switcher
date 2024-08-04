/*
 * This file is part of switcher-gsoap.
 *
 * switcher-gsoap is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * switcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with switcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "soap-ctrl-server.hpp"
#include "switcher/infotree/json-serializer.hpp"
#include "switcher/utils/file-utils.hpp"
#include "switcher/utils/scope-exit.hpp"
#include "switcher/utils/serialize-string.hpp"
#include "webservices/control.nsmap"

// hacking gsoap bug for ubuntu 13.10
#ifdef WITH_IPV6
#define SOAPBINDTO "::"
#else
#define SOAPBINDTO nullptr
#endif

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(SoapCtrlServer,
                                     "SOAPcontrolServer",
                                     "Switcher Web Controler (SOAP)",
                                     "Control switcher through SOAP webservices",
                                     "GPL",
                                     "Nicolas Bouillot");

SoapCtrlServer::SoapCtrlServer(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf)),
      set_port_id_(mmanage<&method::MBag::make_method<std::function<bool(int)>>>(
          "set_port",
          infotree::json::deserialize(
              R"(
                  {
                   "name" : "Set Port",
                   "description" : "set the port used by the soap server",
                   "arguments" : [
                     {
                        "long name" : "Port",
                        "description" : "the port to bind"
                     }
                   ]
                  }
              )"),
          [this](int port) { return set_port(port); })) {
  soap_init(&soap_);
  // release port
  soap_.bind_flags = SO_REUSEADDR;
  soap_.connect_flags = SO_LINGER;
  soap_.accept_flags = SO_LINGER;
  soap_.accept_timeout = 100 * -1000;  // 100ms
}

SoapCtrlServer::~SoapCtrlServer() {
  quit_server_thread_ = true;
  if (thread_.joinable()) thread_.join();
  if (-1 != socket_) soap_closesocket(socket_);
  if (nullptr != service_) {
    soap_destroy(service_);
    soap_end(service_);
    soap_done(service_);
    delete service_;
  }
  soap_destroy(&soap_);
  soap_end(&soap_);
  soap_done(&soap_);
}

Switcher* SoapCtrlServer::get_switcher() { return qcontainer_->get_switcher(); }

bool SoapCtrlServer::set_port(int port) {
  if (0 != port_) {
    sw_warning("soap port can be set only once");
    return false;
  }
  port_ = port;
  auto switcher = get_switcher();
  switcher->set_control_port(port_);
  if (!start()) {
    return false;
  }
  mmanage<&method::MBag::disable>(set_port_id_, std::string("port can be set only once"));
  return true;
}

bool SoapCtrlServer::start() {
  soap_.user = (void*)this;
  quit_server_thread_ = false;
  service_ = new controlService(soap_);
  socket_ = service_->bind(SOAPBINDTO, port_, 100 /* BACKLOG */);
  if (!soap_valid_socket(socket_)) {
    service_->soap_print_fault(stderr);
    delete service_;
    service_ = nullptr;
    return false;
  }

  thread_ = std::thread(&SoapCtrlServer::server_thread, this);
  return true;
}

// gboolean
// SoapCtrlServer::stop_wrapped (gpointer user_data)
// {
//   SoapCtrlServer *context = static_cast<SoapCtrlServer*>(user_data);
//   context->stop ();
//   return TRUE;
// }

// bool
// SoapCtrlServer::stop()
// {
//   quit_server_thread_ = true;
//   if (thread_.joinable())
//     thread_.join();
//   else
//     g_print("++++++++++++++++++++++++++++++ !!!!!! thread not joinable\n");
//   //soap_closesocket(socket_);
//   soap_destroy(&soap_);
//   soap_end(&soap_);
//   soap_done(&soap_);
//   if (nullptr != service_)
//     delete service_;
//   return true;
// }

void SoapCtrlServer::server_thread() {
  // /* run iterative server on port until fatal error */
  // if (context->service_->run(context->port_))
  //   { context->service_->soap_stream_fault(std::cerr);
  //   }
  // return nullptr;

  // for (int i = 1; ; i++)
  while (!quit_server_thread_) {
    SOAP_SOCKET s = service_->accept();
    if (!soap_valid_socket(s)) {
      if (service_->errnum)
        service_->soap_print_fault(stderr);
    } else {
      std::unique_lock<std::mutex> lock(mutex_);
      controlService* tcontrol = service_->copy();
      if (service_->errnum) service_->soap_print_fault(stderr);
      tcontrol->serve();
      delete tcontrol;
    }
  }
}

}  // namespace quiddities
}  // namespace switcher

/**********************************************
 * below is the implementation of the service *
 **********************************************/

int controlService::get_kinds(std::vector<std::string>* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (ctrl_server == nullptr || !(bool)manager) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "controlService::get_kinds: cannot get manager (nullptr)");
    return soap_senderfault("error in get_kinds", s);
  }

  *result = manager->factory<&quiddity::Factory::get_kinds>();

  return SOAP_OK;
}

int controlService::get_kinds_doc(std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (ctrl_server == nullptr || !(bool)manager) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "controlService::get_kinds: cannot get manager (nullptr)");
    return soap_senderfault("error in get_kinds_doc", s);
  }

  *result =
      infotree::json::serialize(manager->factory<&quiddity::Factory::get_kinds_doc>().get());

  return SOAP_OK;
}

int controlService::get_quiddity_description(const std::string& quiddity_nickname,
                                             std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (ctrl_server == nullptr || !(bool)manager) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "controlService::get_kinds: cannot get manager (nullptr)");
    return soap_senderfault("error in get_quiddity_description", s);
  }

  *result = infotree::json::serialize(
      manager
          ->quids<&quiddity::Container::get_quiddity_description>(
              manager->quids<&quiddity::Container::get_id>(quiddity_nickname))
          .get());

  return SOAP_OK;
}

int controlService::get_quiddities_description(std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (ctrl_server == nullptr || !(bool)manager) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s,
            "controlService::get_quiddities_description: cannot get manager "
            "(nullptr)");
    return soap_senderfault("error in get_kinds_doc", s);
  }

  *result = infotree::json::serialize(
      manager->quids<&quiddity::Container::get_quiddities_description>().get());

  return SOAP_OK;
}

int controlService::get_quiddity_nicknames(std::vector<std::string>* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  *result = ctrl_server->get_switcher()->quids<&quiddity::Container::get_nicknames>();
  return SOAP_OK;
}

int controlService::set_property(const std::string& quiddity_nickname,
                                 const std::string& property_name,
                                 const std::string& property_value) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  auto id = qrox ? qrox.get()->prop<&quiddity::property::PBag::get_id>(property_name) : 0;
  if (0 == id) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s,
            "property %s not found for quiddity %s",
            property_name.c_str(),
            quiddity_nickname.c_str());
    return soap_senderfault("set property error", s);
  }
  qrox.get()->prop<&quiddity::property::PBag::set_str>(id, property_value);
  return send_set_property_empty_response(SOAP_OK);
}

int controlService::get_property(const std::string& quiddity_nickname,
                                 const std::string& property_name,
                                 std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  auto id = qrox ? qrox.get()->prop<&quiddity::property::PBag::get_id>(property_name) : 0;
  if (0 == id)
    *result = std::string();
  else
    *result = qrox.get()->prop<&quiddity::property::PBag::get_str>(id);
  return SOAP_OK;
}

int controlService::get_shmpath(const std::string& quiddity_nickname,
                                const std::string& label,
                                std::string* result) {
  using namespace switcher;
  using namespace quiddity::claw;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  *result = std::string();
  if (!qrox) {
    return SOAP_OK;
  }
  auto swid = qrox.get()->claw<&Claw::get_swid>(label);
  if (swid != Ids::kInvalid) {
    *result = qrox.get()->claw<&Claw::get_writer_shmpath>(swid);
  }
  return SOAP_OK;
}

int controlService::create_named_quiddity(const std::string& quiddity_kind,
                                          const std::string& nick_name,
                                          std::string* result) {
  using namespace switcher;

  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto created =
      manager->quids<&quiddity::Container::create>(quiddity_kind, nick_name, nullptr);
  if (created) {
    *result = std::to_string(created.get_id());
  } else {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "%s cannot be created: %s", quiddity_kind.c_str(), created.msg().c_str());
    return soap_senderfault("Quiddity creation error", s);
  }
  return SOAP_OK;
}

int controlService::delete_quiddity(const std::string& quiddity_nickname) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (manager->quids<&quiddity::Container::remove>(
          manager->quids<&quiddity::Container::get_id>(quiddity_nickname)))
    return send_set_property_empty_response(SOAP_OK);
  else {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "%s is not found, not deleting", quiddity_nickname.c_str());
    return send_set_property_empty_response(soap_senderfault("Quiddity creation error", s));
  }
}

int controlService::invoke_method(const std::string& quiddity_nickname,
                                  const std::string& method_name,
                                  std::vector<std::string> args,
                                  std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto tuple_args = std::string();
  for (auto& it : args) {
    if (tuple_args.empty())
      tuple_args = serialize::esc_for_tuple(it);
    else
      tuple_args = tuple_args + "," + serialize::esc_for_tuple(it);
  }

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);

  auto method_id = qrox ? qrox.get()->meth<&quiddity::method::MBag::get_id>(method_name) : 0;
  if (0 != method_id) {
    auto res = qrox.get()->meth<&quiddity::method::MBag::invoke_str>(method_id, tuple_args);
    if (res) {
      *result = res.msg();
      return SOAP_OK;
    }
  }
  char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
  sprintf(s, "invoking %s/%s returned false", quiddity_nickname.c_str(), method_name.c_str());
  return soap_senderfault("Method invocation error", s);
}

int controlService::save(const std::string& file_name, std::string* result) {
  using namespace switcher;

  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (fileutils::save(infotree::json::serialize(manager->get_state().get()), file_name))
    *result = "true";
  else
    *result = "false";
  return SOAP_OK;
}

int controlService::load(const std::string& file_name, std::string* result) {
  using namespace switcher;

  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  manager->reset_state(true);

  if (!manager->load_state(infotree::json::deserialize(fileutils::get_content(file_name)).get())) {
    *result = "false";
    return SOAP_OK;
  }
  *result = "true";
  return SOAP_OK;
}

int controlService::run(const std::string& file_name, std::string* result) {
  using namespace switcher;

  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  if (!manager->load_state(infotree::json::deserialize(fileutils::get_content(file_name)).get())) {
    *result = "false";
    return SOAP_OK;
  }
  *result = "true";
  return SOAP_OK;
}

int controlService::get_information_tree(const std::string& quiddity_nickname,
                                         const std::string& path,
                                         std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  if (qrox) *result = qrox.get()->tree<&InfoTree::serialize_json>(path);
  return SOAP_OK;
}

int controlService::get_user_data(const std::string& quiddity_nickname,
                                  const std::string& path,
                                  std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();
  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  if (qrox) *result = qrox.get()->user_data<&InfoTree::serialize_json>(path);
  return SOAP_OK;
}

int controlService::prune_user_data(const std::string& quiddity_nickname,
                                    const std::string& path,
                                    std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();
  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  if (qrox)
    *result =
        static_cast<bool>(qrox.get()->user_data<&InfoTree::prune>(path)) ? "true" : "false";

  return SOAP_OK;
}

int controlService::graft_user_data(const std::string& quiddity_nickname,
                                    const std::string& path,
                                    const std::string& type,
                                    const std::string& value,
                                    std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  auto res = false;
  if (!qrox) {
  } else if (type == "int") {
    auto deserialized = deserialize::apply<int>(value);
    if (deserialized.first &&
        qrox.get()->user_data<&InfoTree::graft>(path, InfoTree::make(deserialized.second)))
      res = true;
  } else if (type == "float") {
    auto deserialized = deserialize::apply<float>(value);
    if (deserialized.first &&
        qrox.get()->user_data<&InfoTree::graft>(path, InfoTree::make(deserialized.second)))
      res = true;
  } else if (type == "bool") {
    auto deserialized = deserialize::apply<bool>(value);
    if (deserialized.first &&
        qrox.get()->user_data<&InfoTree::graft>(path, InfoTree::make(deserialized.second)))
      res = true;
  } else if (type == "string") {
    if (qrox.get()->user_data<&InfoTree::graft>(path, InfoTree::make(value))) res = true;
  } else {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(s, "type not handled with soap");
    return soap_senderfault("error in get_kinds", s);
  }

  if (res)
    *result = "true";
  else
    *result = "false";

  return SOAP_OK;
}

int controlService::tag_as_array_user_data(const std::string& quiddity_nickname,
                                           const std::string& path,
                                           bool is_array,
                                           std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  if (qrox)
    *result =
        static_cast<bool>(qrox.get()->user_data<&InfoTree::tag_as_array>(path, is_array))
            ? "true"
            : "false";
  return SOAP_OK;
}

int controlService::try_connect(const std::string& reader_quiddity,
                                const std::string& writer_quiddity,
                                std::string* result) {
  using namespace switcher;

  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();
  bool success = false;
  auto qid = manager->quids<&quiddity::Container::get_id>(reader_quiddity);
  if (qid != Ids::kInvalid) {
    auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
    auto wqid = manager->quids<&quiddity::Container::get_id>(writer_quiddity);
    if (qrox && wqid != Ids::kInvalid) {
      *result = qrox.get()->claw<&quiddity::claw::Claw::try_connect>(wqid) != Ids::kInvalid
                    ? "true"
                    : "false";
      success = true;
    }
  }

  if (!success) {
    char* s = reinterpret_cast<char*>(soap_malloc(this, 1024));
    sprintf(
        s, "%s (reader) cannot connect to %s", reader_quiddity.c_str(), writer_quiddity.c_str());
    return soap_senderfault("Quiddity connection error", s);
  }
  return SOAP_OK;
}

int controlService::get_connection_spec(const std::string& quiddity_nickname, std::string* result) {
  using namespace switcher;
  quiddities::SoapCtrlServer* ctrl_server = static_cast<quiddities::SoapCtrlServer*>(this->user);
  Switcher* manager = ctrl_server->get_switcher();

  auto qid = manager->quids<&quiddity::Container::get_id>(quiddity_nickname);
  auto qrox = manager->quids<&quiddity::Container::get_qrox>(qid);
  if (qrox) *result = qrox.get()->conspec<&InfoTree::serialize_json>(".");
  return SOAP_OK;
}
