// Copyright (c) 2014-2016 The Dash Core developers

#ifndef GOVERANCE_EXCEPTIONS_H
#define GOVERANCE_EXCEPTIONS_H

#include <exception>
#include <string>
#include <iostream>

enum governance_exception_type_enum_t {
    /// Default value, normally indicates no exception condition occurred
    GOVERNANCE_EXCEPTION_NONE = 0,
    /// Unusual condition requiring no caller action
    GOVERNANCE_EXCEPTION_WARNING = 1,
    /// Requested operation cannot be performed
    GOVERNANCE_EXCEPTION_PERMANENT_ERROR = 2,
    /// Requested operation not currently possible, may resubmit later
    GOVERNANCE_EXCEPTION_TEMPORARY_ERROR = 3,
    /// Unexpected error (ie. should not happen unless there is a bug in the code)
    GOVERNANCE_EXCEPTION_INTERNAL_ERROR = 4
};

inline std::ostream& operator<<(std::ostream& os, governance_exception_type_enum_t eType)
{
    switch(eType) {
    case GOVERNANCE_EXCEPTION_NONE:
        os << "GOVERNANCE_EXCEPTION_NONE";
        break;
    case GOVERNANCE_EXCEPTION_WARNING:
        os << "GOVERNANCE_EXCEPTION_WARNING";
        break;
    case GOVERNANCE_EXCEPTION_PERMANENT_ERROR:
        os << "GOVERNANCE_EXCEPTION_PERMANENT_ERROR";
        break;
    case GOVERNANCE_EXCEPTION_TEMPORARY_ERROR:
        os << "GOVERNANCE_EXCEPTION_TEMPORARY_ERROR";
        break;
    case GOVERNANCE_EXCEPTION_INTERNAL_ERROR:
        os << "GOVERNANCE_EXCEPTION_INTERNAL_ERROR";
        break;
    }
    return os;
}

/**
 * A class which encapsulates information about a governance exception condition
 *
 * Derives from std::exception so is suitable for throwing
 * (ie. will be caught by a std::exception handler) but may also be used as a
 * normal object.
 */
class CGovernanceException : public std::exception
{
private:
    std::string strMessage;

    governance_exception_type_enum_t eType;

public:
    CGovernanceException(const std::string& strMessageIn = "", governance_exception_type_enum_t eTypeIn = GOVERNANCE_EXCEPTION_NONE)
        : strMessage(),
          eType(eTypeIn)
    {
        std::ostringstream ostr;
        ostr << eType << ":" << strMessageIn;
        strMessage = ostr.str();
    }

    virtual ~CGovernanceException() throw() {}

    virtual const char* what() const throw()
    {
        return strMessage.c_str();
    }

    const std::string& GetMessage() const
    {
        return strMessage;
    }

    governance_exception_type_enum_t GetType() const
    {
        return eType;
    }
};

#endif
