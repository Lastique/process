// Copyright (c) 2006, 2007 Julio M. Merino Vidal
// Copyright (c) 2008 Ilya Sokolov, Boris Schaeling
// Copyright (c) 2009 Boris Schaeling
// Copyright (c) 2010 Felipe Tanus, Boris Schaeling
// Copyright (c) 2011, 2012 Jeff Flinn, Boris Schaeling
// Copyright (c) 2016 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_PROCESS_WINDOWS_EXECUTOR_HPP
#define BOOST_PROCESS_WINDOWS_EXECUTOR_HPP

#include <boost/process/child.hpp>
#include <boost/process/detail/windows/is_running.hpp>
#include <boost/process/detail/traits.hpp>
#include <boost/process/error.hpp>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/detail/winapi/handles.hpp>
#include <boost/detail/winapi/process.hpp>
#include <boost/none.hpp>
#include <system_error>
#include <memory>
#include <atomic>
#include <cstring>


namespace boost { namespace process { namespace detail { namespace windows {

template<typename CharType> struct startup_info;
#if !defined( BOOST_NO_ANSI_APIS )

template<> struct startup_info<char>
{
    typedef ::boost::detail::winapi::STARTUPINFOA_ type;
};
#endif

template<> struct startup_info<wchar_t>
{
    typedef ::boost::detail::winapi::STARTUPINFOW_ type;
};

#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_WIN6

template<typename CharType> struct startup_info_ex;

#if !defined( BOOST_NO_ANSI_APIS )
template<> struct startup_info_ex<char>
{
    typedef ::boost::detail::winapi::STARTUPINFOEXA_ type;
};
#endif

template<> struct startup_info_ex<wchar_t>
{
    typedef ::boost::detail::winapi::STARTUPINFOEXW_ type;
};


#endif

#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_WIN6


template<typename CharT>
struct startup_info_impl
{
    ::boost::detail::winapi::DWORD_ creation_flags = ::boost::detail::winapi::EXTENDED_STARTUPINFO_PRESENT_;

    typedef typename startup_info_ex<CharT>::type startup_info_ex_t;
    typedef typename startup_info<CharT>::type    startup_info_t;

    startup_info_ex_t  startup_info_ex
            {startup_info_t {sizeof(startup_info_t), nullptr, nullptr, nullptr,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, nullptr,
                               ::boost::detail::winapi::invalid_handle_value,
                               ::boost::detail::winapi::invalid_handle_value,
                               ::boost::detail::winapi::invalid_handle_value},
                nullptr
    };
    startup_info_t & startup_info =  startup_info_ex.StartupInfo;
};


#else

template<typename CharT>
struct startup_info_impl
{
    typedef typename startup_info<CharT>::type    startup_info_t;

    ::boost::detail::winapi::DWORD_ creation_flags = 0;
    startup_info_t          startup_info
            {sizeof(startup_info_t), nullptr, nullptr, nullptr,
             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, nullptr,
             ::boost::detail::winapi::invalid_handle_value,
             ::boost::detail::winapi::invalid_handle_value,
             ::boost::detail::winapi::invalid_handle_value};

    startup_info_t & get_startup_info() { return startup_info; }

};

#endif




template<typename Sequence>
struct executor : startup_info_impl<char>
{
    typedef typename ::boost::process::detail::has_error_handler<Sequence>::type has_error_handler;

    executor(Sequence & seq) : seq(seq)
    {
    }
    void internal_error_handle(const std::error_code &ec, const char* msg, boost::mpl::true_ )
    {
        this->ec = ec;
    }
    void internal_error_handle(const std::error_code &ec, const char* msg, boost::mpl::false_ )
    {
        throw std::system_error(ec, msg);
    }

    void internal_throw(boost::mpl::true_,  std::error_code &ec ) {}
    void internal_throw(boost::mpl::false_, std::error_code &ec ) {throw std::system_error(ec);}



    struct on_setup_t
    {
        executor & exec;
        on_setup_t(executor & exec) : exec(exec) {};
        template<typename T>
        void operator()(T & t) const {t.on_setup(exec);}
    };

    struct on_error_t
    {
        executor & exec;
        const std::error_code & error;
        on_error_t(executor & exec, const std::error_code & error) : exec(exec), error(error) {};
        template<typename T>
        void operator()(T & t) const {t.on_error(exec, error);}
    };

    struct on_success_t
    {
        executor & exec;
        on_success_t(executor & exec) : exec(exec) {};
        template<typename T>
        void operator()(T & t) const {t.on_success(exec);}
    };


    child operator()()
    {
        on_setup_t on_setup(*this);
        boost::fusion::for_each(seq, on_setup);

        if (ec)
        {
            on_error_t on_error(*this, ec);
            boost::fusion::for_each(seq, on_error);
            return child();
        }



        //NOTE: The non-cast cmd-line string can only be modified by the wchar_t variant which is currently disabled.
        int err_code = ::boost::detail::winapi::create_process(
            exe,                                        //       LPCSTR_ lpApplicationName,
            const_cast<char*>(cmd_line),                //       LPSTR_ lpCommandLine,
            proc_attrs,                                 //       LPSECURITY_ATTRIBUTES_ lpProcessAttributes,
            thread_attrs,                               //       LPSECURITY_ATTRIBUTES_ lpThreadAttributes,
            inherit_handles,                            //       INT_ bInheritHandles,
            creation_flags,                             //       DWORD_ dwCreationFlags,
            reinterpret_cast<void*>(const_cast<char*>(env)),  //     LPVOID_ lpEnvironment,
            work_dir,                                   //       LPCSTR_ lpCurrentDirectory,
            &this->startup_info,                        //       LPSTARTUPINFOA_ lpStartupInfo,
            &proc_info);                                //       LPPROCESS_INFORMATION_ lpProcessInformation)

        child c{child_handle(proc_info), exit_status};

        if (err_code != 0)
        {
            ec.clear();
            on_success_t on_success(*this);
            boost::fusion::for_each(seq, on_success);
        }

        if ((err_code == 0) || ec)
        {
            auto last_error = (err_code == 0) ? boost::process::detail::get_last_error() : ec;

            on_error_t on_error(*this, last_error);
            boost::fusion::for_each(seq, on_error);
            internal_throw(has_error_handler(), last_error);
            return child();
        }
        else
            return c;

    }

    void handle_error(std::error_code & ec, const char* msg = "Unknown Error.")
    {
        internal_error_handle(ec, msg, has_error_handler());
    }

    ::boost::detail::winapi::LPSECURITY_ATTRIBUTES_ proc_attrs   = nullptr;
    ::boost::detail::winapi::LPSECURITY_ATTRIBUTES_ thread_attrs = nullptr;
    ::boost::detail::winapi::BOOL_ inherit_handles = false;
    const char * work_dir = nullptr;
    const char * cmd_line = nullptr;
    const char * exe      = nullptr;
    const char * env      = nullptr;

    std::error_code ec{0, std::system_category()};

    Sequence & seq;
    ::boost::detail::winapi::PROCESS_INFORMATION_ proc_info{nullptr, nullptr, 0,0};

    std::shared_ptr<std::atomic<int>> exit_status = std::make_shared<std::atomic<int>>(still_active);

};



template<typename Tup>
executor<Tup> make_executor(Tup & tup)
{
    return executor<Tup>(tup);
}


}}}}

#endif
