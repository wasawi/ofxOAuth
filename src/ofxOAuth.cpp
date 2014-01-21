// =============================================================================
//
// Copyright (c) 2010-2013 Christopher Baker <http://christopherbaker.net>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// =============================================================================


#include "ofxOAuth.h"


//------------------------------------------------------------------------------
ofxOAuth::ofxOAuth(): ofxOAuthVerifierCallbackInterface()
{
    oauthMethod = OFX_OA_HMAC;  // default
    httpMethod  = OFX_HTTP_GET; // default

    const char* v = getenv("CURLOPT_CAINFO");
    if(v != NULL) _old_curlopt_cainfo = v;
    
    // this Certificate Authority bundle is extracted 
    // from mozilla.org.pem, which can be found here
    //
    // http://curl.haxx.se/ca/
    // http://curl.haxx.se/ca/cacert.pem
    //
    // If it is not placed in the default (PROJECT)/data/
    // directory, an different location can 
    // can be set by calling:
    
    setSSLCACertificateFile("cacert.pem");
    
    // this setter sets an environmental variable,
    // which is accessed by liboauth whenever libcurl
    // calls are executed.
    
    callbackConfirmed = false;
    
    verificationRequested = false;
    accessFailed = false;
    accessFailedReported = false;
    
    apiName = "GENERIC"; 
    
    credentialsPathname = "credentials.xml";
    verifierCallbackServerDocRoot = "VerifierCallbackServer/";
    vertifierCallbackServerPort = -1;
    enableVerifierCallbackServer = true;
    
    ofAddListener(ofEvents().update,this,&ofxOAuth::update);
}

//------------------------------------------------------------------------------
ofxOAuth::~ofxOAuth()
{
    // be nice and set it back, if there was 
    // something there when we started.
    if(!_old_curlopt_cainfo.empty())
    {
        setenv("CURLOPT_CAINFO",_old_curlopt_cainfo.c_str(),true);
    }
    else
    {
        unsetenv("CURLOPT_CAINFO");
    }

    ofRemoveListener(ofEvents().update,this,&ofxOAuth::update);
}

//------------------------------------------------------------------------------
void ofxOAuth::setup(const std::string& _apiURL,
                     const std::string& _requestTokenUrl,
                     const std::string& _accessTokenUrl,
                     const std::string& _authorizeUrl,
                     const std::string& _consumerKey,
                     const std::string& _consumerSecret)
{
    setApiURL(_apiURL,false);
    setRequestTokenURL(_requestTokenUrl);
    setAccessTokenURL(_accessTokenUrl);
    setAuthorizationURL(_authorizeUrl);
    setConsumerKey(_consumerKey);
    setConsumerSecret(_consumerSecret);

    loadCredentials();
}


//------------------------------------------------------------------------------
void ofxOAuth::setup(const std::string& _apiURL,
                     const std::string& _consumerKey,
                     const std::string& _consumerSecret)
{
    setApiURL(_apiURL);
    setConsumerKey(_consumerKey);
    setConsumerSecret(_consumerSecret);

    loadCredentials();
}

//------------------------------------------------------------------------------
void ofxOAuth::update(ofEventArgs& args)
{
    if(accessFailed)
    {
        if(!accessFailedReported)
        {
            ofLogError("ofxOAuth::update") << "Access failed.";
            accessFailedReported = true;
        }
    }
    else if(accessToken.empty() || accessTokenSecret.empty())
    {
        if(requestTokenVerifier.empty())
        {
            if(requestToken.empty())
            {
                if(enableVerifierCallbackServer)
                {
                    if(verifierCallbackServer == NULL)
                    {
                        verifierCallbackServer = std::shared_ptr<ofxOAuthVerifierCallbackServer>(new ofxOAuthVerifierCallbackServer(this,verifierCallbackServerDocRoot, vertifierCallbackServerPort));
                        verifierCallbackURL = verifierCallbackServer->getURL();
                        verifierCallbackServer->start();
                    }
                }
                else
                {
                    // nichts
                    ofLogVerbose("ofxOAuth::update") << "Server disabled, expecting verifiy key input via a non server method (i.e. text input.)";
                    ofLogVerbose("ofxOAuth::update") << "\t\tThis is done via 'oob' (Out-of-band OAuth authentication).";
                    ofLogVerbose("ofxOAuth::update") << "\t\tCall setRequestTokenVerifier() with a verification code to continue.";
                }

                obtainRequestToken();
            }
            else
            {
                if(!verificationRequested)
                {
                    requestUserVerification();
                    verificationRequested = true;
                    ofLogVerbose("ofxOAuth::update") << "Waiting for user verification (need the pin number / requestTokenVerifier!)";
                    ofLogVerbose("ofxOAuth::update") << "\t\tIf the server is enabled, then this will happen as soon as the user is redirected.";
                    ofLogVerbose("ofxOAuth::update") << "\t\tIf the server is disabled, verification must be done via 'oob'";
                    ofLogVerbose("ofxOAuth::update") << "\t\t(Out-of-band OAuth authentication). Call setRequestTokenVerifier()";
                    ofLogVerbose("ofxOAuth::update") << "\t\twith a verification code to continue.";
                }
                else
                {
                    // nichts
                }
            }
        }
        else
        {
            if(!accessFailed)
            {
                verificationRequested = false;
                if(verifierCallbackServer != NULL)
                {
                    verifierCallbackServer->stop(); // stop the server
                    verifierCallbackServer.reset(); // destroy the server, setting it back to null
                }
                obtainAccessToken();
            }
        } 
    }
    else
    {
        if(verifierCallbackServer != NULL)
        {
            // go ahead and free that memory
            verifierCallbackServer->stop(); // stop the server
            verifierCallbackServer.reset(); // destroy the server, setting it back to null
        }
    }
}

//------------------------------------------------------------------------------
std::string ofxOAuth::get(const std::string& uri, const std::string& query)
{
    std::string result = "";
        
    if(apiURL.empty())
    {
        ofLogError("ofxOAuth::get") << "No api URL specified.";
        return result;
    }
    
    if(consumerKey.empty())
    {
        ofLogError("ofxOAuth::get") << "No consumer key specified.";
        return result;
    }
    
    if(consumerSecret.empty())
    {
        ofLogError("ofxOAuth::get") << "No consumer secret specified.";
        return result;
    }
    
    if(accessToken.empty())
    {
        ofLogError("ofxOAuth::get") << "No access token specified.";
        return result;
    }

    if(accessTokenSecret.empty())
    {
        ofLogError("ofxOAuth::get") << "No access token secret specified.";
        return result;
    }

    std::string req_url;
    std::string req_hdr;
    std::string http_hdr;
    
    std::string reply;
    
    // oauth_sign_url2 (see oauth.h) in steps
    int  argc   = 0;
    char **argv = NULL;
    
    // break apart the url parameters to they can be signed below
    // if desired we can also pass in additional patermeters (like oath* params)
    // here.  For instance, if ?oauth_callback=XXX is defined in this url,
    // it will be parsed and used in the Authorization header.
    
    std::string url = apiURL + uri + "?" + query;
    
    argc = oauth_split_url_parameters(url.c_str(), &argv);
    
    // sign the array.
    oauth_sign_array2_process(&argc, 
                              &argv,
                              NULL,							//< postargs (unused)
                              _getOAuthMethod(),			// hash type, OA_HMAC, OA_RSA, OA_PLAINTEXT
                              _getHttpMethod().c_str(),		//< HTTP method (defaults to "GET")
                              consumerKey.c_str(),			//< consumer key - posted plain text
                              consumerSecret.c_str(),		//< consumer secret - used as 1st part of secret-key
                              accessToken.c_str(),			//< token key - posted plain text in URL
                              accessTokenSecret.c_str());	//< token secret - used as 2st part of secret-key
    
    ofLogVerbose("ofxOAuth::get") << "-------------------";
    ofLogVerbose("ofxOAuth::get") << "consumerKey          >" << consumerKey << "<";
    ofLogVerbose("ofxOAuth::get") << "consumerSecret       >" << consumerSecret << "<";
    ofLogVerbose("ofxOAuth::get") << "requestToken         >" << requestToken << "<";
    ofLogVerbose("ofxOAuth::get") << "requestTokenVerifier >" << requestTokenVerifier << "<";
    ofLogVerbose("ofxOAuth::get") << "requestTokenSecret   >" << requestTokenSecret << "<";
    ofLogVerbose("ofxOAuth::get") << "accessToken          >" << accessToken << "<";
    ofLogVerbose("ofxOAuth::get") << "accessTokenSecret    >" << accessTokenSecret << "<";
    ofLogVerbose("ofxOAuth::get") << "-------------------";

    // collect any parameters in our list that need to be placed in the request URI
    req_url = oauth_serialize_url_sep(argc, 0, argv, const_cast<char *>("&"), 1); 
    
    // collect any of the oauth parameters for inclusion in the HTTP Authorization header.
    req_hdr = oauth_serialize_url_sep(argc, 1, argv, const_cast<char *>(", "), 6); // const_cast<char *>() is to avoid Deprecated 
    
    // look at url parameters to be signed if you want.
    if(ofGetLogLevel() <= OF_LOG_VERBOSE)
        for (int i=0;i<argc; i++) ofLogVerbose("ofxOAuth::get") << " : " << i << ":" << argv[i];
    
    // free our parameter arrays that were allocated during parsing above    
    oauth_free_array(&argc, &argv);
    
    // construct the Authorization header.  Include realm information if available.
    if(!realm.empty())
    {
        // Note that (optional) 'realm' is not to be 
        // included in the oauth signed parameters and thus only added here.
        // see 9.1.1 in http://oauth.net/core/1.0/#anchor14
        http_hdr = "Authorization: OAuth realm=\"" + realm + "\", " + req_hdr; 
    }
    else
    {
        http_hdr = "Authorization: OAuth " + req_hdr; 
    }
    
    ofLogVerbose("ofxOAuth::get") << "request URL    >" << req_url << "<";
    ofLogVerbose("ofxOAuth::get") << "request HEADER >" << req_hdr << "<";
    ofLogVerbose("ofxOAuth::get") << "http    HEADER >" << http_hdr << "<";
    
    reply = oauth_http_get2(req_url.c_str(),   // the base url to get
                            NULL,              // the query string to send
                            http_hdr.c_str()); // Authorization header is included here
    
    if (reply.empty())
    {
        ofLogVerbose("ofxOAuth::get") << "HTTP get request failed.";
    }
    else
    {
        ofLogVerbose("ofxOAuth::get") << "HTTP-Reply: " << reply;
        result = reply;
    }
    
    return result;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::post(const std::string& uri, const std::string& query)
{
	// we will use POST as HTTP method
	httpMethod = OFX_HTTP_POST;
	
    string result = "";
    
	if(apiURL.empty())
    {
        ofLogError("ofxOAuth::post") << "No api URL specified.";
        return result;
    }
    
    if(consumerKey.empty())
    {
        ofLogError("ofxOAuth::post") << "No consumer key specified.";
        return result;
    }
    
    if(consumerSecret.empty())
    {
        ofLogError("ofxOAuth::post") << "No consumer secret specified.";
        return result;
    }
    
    if(accessToken.empty())
    {
        ofLogError("ofxOAuth::post") << "No access token specified.";
        return result;
    }
	
    if(accessTokenSecret.empty())
    {
        ofLogError("ofxOAuth::post") << "No access token secret specified.";
        return result;
    }
	std::string req_url;
    std::string req_hdr;
    std::string http_hdr;
    
    std::string reply;
    
    // oauth_sign_url2 (see oauth.h) in steps
    int  argc   = NULL;
    char **argv = NULL;
	
    // break apart the url parameters so they can be signed below
    // if desired we can also pass in additional patermeters (like oath* params)
    // here.  For instance, if ?oauth_callback=XXX is defined in this url,
    // it will be parsed and used in the Authorization header.
    
    std::string url = apiURL + uri + "?" + query;
    
    argc = oauth_split_url_parameters(url.c_str(), &argv);

	// sign the array.
	oauth_sign_array2_process(&argc,						// argcp pointer to array length int
                              &argv,						// argvp pointer to array values
							  NULL,							//< postargs This parameter points to an area where the return value is stored.
															// If 'postargs' is NULL, no value is stored.
                              _getOAuthMethod(),			// hash type, OA_HMAC, OA_RSA, OA_PLAINTEXT
                              _getHttpMethod().c_str(),		//< HTTP method (defaults to "GET")
                              consumerKey.c_str(),			//< consumer key - posted plain text
                              consumerSecret.c_str(),		//< consumer secret - used as 1st part of secret-key
                              accessToken.c_str(),			//< token key - posted plain text in URL
                              accessTokenSecret.c_str());	//< token secret - used as 2st part of secret-key
	
    ofLogVerbose("ofxOAuth::post") << "-------------------";
    ofLogVerbose("ofxOAuth::post") << "consumerKey          >" << consumerKey << "<";
    ofLogVerbose("ofxOAuth::post") << "consumerSecret       >" << consumerSecret << "<";
    ofLogVerbose("ofxOAuth::post") << "requestToken         >" << requestToken << "<";
    ofLogVerbose("ofxOAuth::post") << "requestTokenVerifier >" << requestTokenVerifier << "<";
    ofLogVerbose("ofxOAuth::post") << "requestTokenSecret   >" << requestTokenSecret << "<";
    ofLogVerbose("ofxOAuth::post") << "accessToken          >" << accessToken << "<";
    ofLogVerbose("ofxOAuth::post") << "accessTokenSecret    >" << accessTokenSecret << "<";
    ofLogVerbose("ofxOAuth::post") << "-------------------";
	
    // collect any parameters in our list that need to be placed in the request URI
    req_url = oauth_serialize_url_sep(argc, 0, argv, const_cast<char *>("&"), 1);

    // collect any of the oauth parameters for inclusion in the HTTP Authorization header.
    req_hdr = oauth_serialize_url_sep(argc, 1, argv, const_cast<char *>(", "), 2); // const_cast<char *>() is to avoid Deprecated
    
    // look at url parameters to be signed if you want.
    if(ofGetLogLevel() <= OF_LOG_VERBOSE)
        for (int i=0;i<argc; i++) ofLogVerbose("ofxOAuth::post") << " : " << i << ":" << argv[i];
    
    // free our parameter arrays that were allocated during parsing above
    oauth_free_array(&argc, &argv);
    
    // construct the Authorization header.  Include realm information if available.
    if(!realm.empty())
    {
        // Note that (optional) 'realm' is not to be
        // included in the oauth signed parameters and thus only added here.
        // see 9.1.1 in http://oauth.net/core/1.0/#anchor14
        http_hdr = "Authorization: OAuth realm=\"" + realm + "\", " + req_hdr;
    }
    else
    {
        http_hdr = "Authorization: OAuth " + req_hdr;
    }
	
    ofLogVerbose("ofxOAuth::post") << "request URL    >" << req_url << "<";
    ofLogVerbose("ofxOAuth::post") << "request HEADER >" << req_hdr << "<";
    ofLogVerbose("ofxOAuth::post") << "http    HEADER >" << http_hdr << "<";
	
    reply = oauth_http_post2(req_url.c_str(),   // the base url to query
							 "",				// the query string to send
							 http_hdr.c_str());	// Authorization header is included here
    
    if (reply.empty())
    {
        ofLogVerbose("ofxOAuth::post") << "HTTP get request failed.";
    }
    else
    {
        ofLogVerbose("ofxOAuth::post") << "HTTP-Reply: " << reply;
        result = reply;
    }
    
	return result;
}

//------------------------------------------------------------------------------
std::map<string, string> ofxOAuth::obtainRequestToken()
{
    map<std::string, std::string> returnParams;

    if(requestTokenURL.empty())
    {
        ofLogError("ofxOAuth::obtainRequestToken") << "No request token URL specified.";
        return returnParams;
    }
    
    if(consumerKey.empty())
    {
        ofLogError("ofxOAuth::obtainRequestToken") << "No consumer key specified.";
        return returnParams;
    }

    if(consumerSecret.empty())
    {
        ofLogError("ofxOAuth::obtainRequestToken") << "No consumer secret specified.";
        return returnParams;
    }

    std::string req_url;
    std::string req_hdr;
    std::string http_hdr;

    std::string reply;
    
    // oauth_sign_url2 (see oauth.h) in steps
    int  argc   = 0;
    char **argv = NULL;
    
    // break apart the url parameters to they can be signed below
    // if desired we can also pass in additional patermeters (like oath* params)
    // here.  For instance, if ?oauth_callback=XXX is defined in this url,
    // it will be parsed and used in the Authorization header.
    argc = oauth_split_url_parameters(requestTokenURL.c_str(), &argv);
    
    // add the authorization callback url info if available
    if(!getVerifierCallbackURL().empty())
    {
        std::string callbackParam = "oauth_callback=" + getVerifierCallbackURL();
        oauth_add_param_to_array(&argc, &argv, callbackParam.c_str());
    }

    // NOTE BELOW:
    /*
     
    FOR GOOGLE:
    Authorization header of a GET or POST request. Use "Authorization: OAuth". All parameters listed above can go in the header, except for scope and xoauth_displayname, which must go either in the body or in the URL as a query parameter. The example below puts them in the body of the request.
    
     https://developers.google.com/accounts/docs/OAuth_ref#RequestToken
     */
    
    if(!getApplicationDisplayName().empty())
    {
        std::string displayNameParam = "xoauth_displayname=" + getApplicationDisplayName();
        oauth_add_param_to_array(&argc, &argv, displayNameParam.c_str());
    }
    
    if(!getApplicationScope().empty())
    {
        // TODO: this will not be integrated correctly by lib oauth
        // b/c it does not have a oauth / xoauth prefix
        // XXXXXXXXXX
        std::string scopeParam = "scope=" + getApplicationScope();
        oauth_add_param_to_array(&argc, &argv, scopeParam.c_str());
    }
    
    
    // NOTE: if desired, normal oatuh parameters, such as oauth_nonce could be overriden here
    // rathern than having them auto-calculated using the oauth_sign_array2_process method
    //oauth_add_param_to_array(&argc, &argv, "oauth_nonce=xxxxxxxpiOuDKDAmwHKZXXhGelPc4cJq");

    // sign the array.
    oauth_sign_array2_process(&argc, 
                              &argv,
                              NULL, //< postargs (unused)
                              _getOAuthMethod(), // hash type, OA_HMAC, OA_RSA, OA_PLAINTEXT
                              _getHttpMethod().c_str(), //< HTTP method (defaults to "GET")
                              consumerKey.c_str(), //< consumer key - posted plain text
                              consumerSecret.c_str(), //< consumer secret - used as 1st part of secret-key
                              NULL,  //< token key - posted plain text in URL
                              NULL); //< token secret - used as 2st part of secret-key
    
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "-------------------";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "consumerKey          >" << consumerKey << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "consumerSecret       >" << consumerSecret << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "requestToken         >" << requestToken << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "requestTokenVerifier >" << requestTokenVerifier << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "requestTokenSecret   >" << requestTokenSecret << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "accessToken          >" << accessToken << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "accessTokenSecret    >" << accessTokenSecret << "<";
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "-------------------";

    // collect any parameters in our list that need to be placed in the request URI
    req_url = oauth_serialize_url_sep(argc, 0, argv, const_cast<char *>("&"), 1); 

    // collect any of the oauth parameters for inclusion in the HTTP Authorization header.
    req_hdr = oauth_serialize_url_sep(argc, 1, argv, const_cast<char *>(", "), 6); // const_cast<char *>() is to avoid Deprecated 

    // look at url parameters to be signed if you want.
    if(ofGetLogLevel() <= OF_LOG_VERBOSE)
        for (int i=0;i<argc; i++) ofLogVerbose("ofxOAuth::obtainRequestToken") << i << " >" << argv[i] << "<";

    // free our parameter arrays that were allocated during parsing above    
    oauth_free_array(&argc, &argv);
    
    // construct the Authorization header.  Include realm information if available.
    if(!realm.empty())
    {
        // Note that (optional) 'realm' is not to be 
        // included in the oauth signed parameters and thus only added here.
        // see 9.1.1 in http://oauth.net/core/1.0/#anchor14
        http_hdr = "Authorization: OAuth realm=\"" + realm + "\", " + req_hdr; 
    }
    else
    {
        http_hdr = "Authorization: OAuth " + req_hdr;
    }

    ofLogVerbose("ofxOAuth::obtainRequestToken") << "Request URL    = " << req_url;
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "Request HEADER = " << req_hdr;
    ofLogVerbose("ofxOAuth::obtainRequestToken") << "http    HEADER = " << http_hdr;
    
    reply = oauth_http_get2(req_url.c_str(),   // the base url to get
                            NULL,              // the query string to send
                            http_hdr.c_str()); // Authorization header is included here
    
    if (reply.empty())
    {
        ofLogVerbose("ofxOAuth::obtainRequestToken") << "HTTP request for an oauth request-token failed.";
    }
    else
    {
        ofLogVerbose("ofxOAuth::obtainRequestToken") << "HTTP-Reply: " << reply;

        // could use oauth_split_url_parameters here.
        std::vector<std::string> params = ofSplitString(reply, "&", true);

        for(int i = 0; i < params.size(); i++)
        {
            std::vector<std::string> tokens = ofSplitString(params[i], "=");
            if(tokens.size() == 2)
            {
                returnParams[tokens[0]] = tokens[1];
                
                if(Poco::icompare(tokens[0],"oauth_token") == 0)
                {
                    requestToken = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"oauth_token_secret") == 0)
                {
                    requestTokenSecret = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"oauth_callback_confirmed") == 0)
                {
                    callbackConfirmed = ofToBool(tokens[1]);
                }
                else if(Poco::icompare(tokens[0],"oauth_problem") == 0)
                {
                    ofLogError("ofxOAuth::obtainRequestToken") <<  "Got oauth problem: " << tokens[1];
                }
                else
                {
                    ofLogNotice("ofxOAuth::obtainRequestToken") << "Got an unknown parameter: " << tokens[0] << "=" + tokens[1];
                }
            }
            else
            {
                ofLogWarning("ofxOAuth::obtainRequestToken") <<  "Return parameter did not have 2 values: " << params[i] << " - skipping.";
            }
        }
    }
    
    if(requestTokenSecret.empty())
    {
        ofLogWarning("ofxOAuth::obtainRequestToken") << "Request token secret not returned.";
        accessFailed = true;
    }

    if(requestToken.empty())
    {
        ofLogWarning("ofxOAuth::obtainRequestToken") << "Request token not returned.";
        accessFailed = true;
    }

    
    return returnParams;
}

//------------------------------------------------------------------------------
std::map<std::string,std::string> ofxOAuth::obtainAccessToken()
{
    std::map<std::string,std::string> returnParams;
    
    if(accessTokenURL.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No access token URL specified.";
        return returnParams;
    }
    
    if(consumerKey.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No consumer key specified.";
        return returnParams;
    }
    
    if(consumerSecret.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No consumer secret specified.";
        return returnParams;
    }
    
    if(requestToken.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No request token specified.";
        return returnParams;
    }
    
    if(requestTokenSecret.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No request token secret specified.";
        return returnParams;
    }
    
    if(requestTokenVerifier.empty())
    {
        ofLogError("ofxOAuth::obtainAccessToken") << "No request token verifier specified.";
        return returnParams;
    }
    
    std::string req_url;
    std::string req_hdr;
    std::string http_hdr;
    
    std::string reply;
    
    // oauth_sign_url2 (see oauth.h) in steps
    int  argc   = 0;
    char **argv = NULL;
    
    // break apart the url parameters to they can be signed below
    // if desired we can also pass in additional patermeters (like oath* params)
    // here.  For instance, if ?oauth_callback=XXX is defined in this url,
    // it will be parsed and used in the Authorization header.
    argc = oauth_split_url_parameters(getAccessTokenURL().c_str(), &argv);
    
    // add the verifier param
    std::string verifierParam = "oauth_verifier=" + requestTokenVerifier;
    oauth_add_param_to_array(&argc, &argv, verifierParam.c_str());

    // NOTE: if desired, normal oauth parameters, such as oauth_nonce could be overriden here
    // rathern than having them auto-calculated using the oauth_sign_array2_process method
    //oauth_add_param_to_array(&argc, &argv, "oauth_nonce=xxxxxxxpiOuDKDAmwHKZXXhGelPc4cJq");
    
    // sign the array.
    oauth_sign_array2_process(&argc, 
                              &argv,
                              NULL, //< postargs (unused)
                              _getOAuthMethod(), // hash type, OA_HMAC, OA_RSA, OA_PLAINTEXT
                              _getHttpMethod().c_str(), //< HTTP method (defaults to "GET")
                              consumerKey.c_str(), //< consumer key - posted plain text
                              consumerSecret.c_str(), //< consumer secret - used as 1st part of secret-key
                              requestToken.c_str(),  //< token key - posted plain text in URL
                              requestTokenSecret.c_str()); //< token secret - used as 2st part of secret-key

    ofLogVerbose("ofxOAuth::obtainAccessToken") << "-------------------";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "consumerKey          >" << consumerKey << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "consumerSecret       >" << consumerSecret << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "requestToken         >" << requestToken << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "requestTokenVerifier >" << requestTokenVerifier << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "requestTokenSecret   >" << requestTokenSecret << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "accessToken          >" << accessToken << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "accessTokenSecret    >" << accessTokenSecret << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "-------------------";
    
    // collect any parameters in our list that need to be placed in the request URI
    req_url = oauth_serialize_url_sep(argc, 0, argv, const_cast<char *>("&"), 1); 
    
    // collect any of the oauth parameters for inclusion in the HTTP Authorization header.
    req_hdr = oauth_serialize_url_sep(argc, 1, argv, const_cast<char *>(", "), 6); // const_cast<char *>() is to avoid Deprecated 
    
    // look at url parameters to be signed if you want.
    if(ofGetLogLevel() <= OF_LOG_VERBOSE)
    {
        for(int i=0; i < argc; i++)
        {
            ofLogVerbose("ofxOAuth::obtainAccessToken") << i << " >" << argv[i] << "<";
        }
    }
    
    // free our parameter arrays that were allocated during parsing above    
    oauth_free_array(&argc, &argv);
    
    // construct the Authorization header.  Include realm information if available.
    if(!realm.empty())
    {
        // Note that (optional) 'realm' is not to be 
        // included in the oauth signed parameters and thus only added here.
        // see 9.1.1 in http://oauth.net/core/1.0/#anchor14
        http_hdr = "Authorization: OAuth realm=\"" + realm + "\", " + req_hdr; 
    }
    else
    {
        http_hdr = "Authorization: OAuth " + req_hdr;
    }
    
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "request URL    >" << req_url << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "request HEADER >" << req_hdr << "<";
    ofLogVerbose("ofxOAuth::obtainAccessToken") << "http    HEADER >" << http_hdr << "<";
    
    reply = oauth_http_get2(req_url.c_str(),   // the base url to get
                            NULL,              // the query string to send
                            http_hdr.c_str()); // Authorization header is included here
    
    
    if (reply.empty())
    {
        ofLogVerbose("ofxOAuth::obtainAccessToken") << "HTTP request for an oauth request-token failed.";
    }
    else
    {
        ofLogVerbose("ofxOAuth::obtainAccessToken") << "HTTP-Reply >" << reply << "<";
        
        // could use oauth_split_url_parameters here.
        std::vector<std::string> params = ofSplitString(reply, "&", true);
        
        for(int i = 0; i < params.size(); i++)
        {
            std::vector<std::string> tokens = ofSplitString(params[i], "=");
            if(tokens.size() == 2)
            {
                returnParams[tokens[0]] = tokens[1];
                
                if(Poco::icompare(tokens[0],"oauth_token") == 0)
                {
                    accessToken = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"oauth_token_secret") == 0)
                {
                    accessTokenSecret = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"encoded_user_id") == 0)
                {
                    encodedUserId = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"user_id") == 0)
                {
                    userId = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"screen_name") == 0)
                {
                    screenName = tokens[1];
                }
                else if(Poco::icompare(tokens[0],"oauth_problem") == 0)
                {
                    ofLogError("ofxOAuth::obtainAccessToken") << "Got oauth problem: " << tokens[1];
                }
                else
                {
                    ofLogNotice("ofxOAuth::obtainAccessToken") << "got an unknown parameter: " << tokens[0] << "=" << tokens[1];
                }
            }
            else
            {
                ofLogWarning("ofxOAuth::obtainAccessToken") << "Return parameter did not have 2 values: "  << params[i] << " - skipping.";
            }
        }
    }
    
    if(accessTokenSecret.empty())
    {
        ofLogWarning("ofxOAuth::obtainAccessToken") << "Access token secret not returned.";
        accessFailed = true;
    }
    
    if(accessToken.empty())
    {
        ofLogWarning("ofxOAuth::obtainAccessToken") << "Access token not returned.";
        accessFailed = true;
    }
    
    // save it to an xml file!
    saveCredentials();
    
    return returnParams;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::requestUserVerification(bool launchBrowser)
{
    return requestUserVerification("",launchBrowser);
}

//--------------------------------------------------------------
std::string ofxOAuth::requestUserVerification(std::string additionalAuthParams,
                                              bool launchBrowser)
{
    
    std::string url = getAuthorizationURL();
    
    if(url.empty())
    {
        ofLogError("ofxOAuth::requestUserVerification") << "Authorization URL is not set.";
        return "";
    }
    
    url += "oauth_token=";
    url += getRequestToken();
    url += additionalAuthParams;

    if(launchBrowser) ofLaunchBrowser(url);

    return url;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getApiURL()
{
    return apiURL;
}

//------------------------------------------------------------------------------
void ofxOAuth::setApiURL(const std::string &v, bool autoSetEndpoints)
{
    apiURL = v; 
    if(autoSetEndpoints)
    {
        setRequestTokenURL(apiURL + "/oauth/request_token");
        setAccessTokenURL(apiURL + "/oauth/access_token");
        setAuthorizationURL(apiURL + "/oauth/authorize");
    }
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getRequestTokenURL()
{
    return requestTokenURL;
}

//------------------------------------------------------------------------------
void ofxOAuth::setRequestTokenURL(const std::string& v)
{
    requestTokenURL = appendQuestionMark(v);
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getAccessTokenURL()
{
    return accessTokenURL;
}

//------------------------------------------------------------------------------
void ofxOAuth::setAccessTokenURL(const std::string& v)
{
    accessTokenURL = appendQuestionMark(v);
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getAuthorizationURL()
{
    return authorizationURL;
}

//------------------------------------------------------------------------------
void ofxOAuth::setAuthorizationURL(const std::string& v)
{
    authorizationURL = appendQuestionMark(v);
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getVerifierCallbackURL()
{
    return verifierCallbackURL;
}

//------------------------------------------------------------------------------
void ofxOAuth::setVerifierCallbackURL(const std::string& v)
{
    verifierCallbackURL = v;
}

//------------------------------------------------------------------------------
void ofxOAuth::setApplicationDisplayName(const std::string& v)
{
    applicationDisplayName = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getApplicationDisplayName()
{
    return applicationDisplayName;
}

//------------------------------------------------------------------------------
void ofxOAuth::setApplicationScope(const std::string& v)
{
    // google specific
    applicationScope = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getApplicationScope()
{
    return applicationScope;
}

//------------------------------------------------------------------------------
bool ofxOAuth::isVerifierCallbackServerEnabled()
{
    return enableVerifierCallbackServer;
}

//------------------------------------------------------------------------------
void ofxOAuth::setVerifierCallbackServerDocRoot(const std::string& v)
{
    verifierCallbackServerDocRoot = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getVerifierCallbackServerDocRoot()
{
    return verifierCallbackServerDocRoot;
}

//------------------------------------------------------------------------------
bool ofxOAuth::isVerifierCallbackPortSet() const
{
    return vertifierCallbackServerPort > 0;
}

//------------------------------------------------------------------------------
int ofxOAuth::getVerifierCallbackServerPort() const
{
    return vertifierCallbackServerPort;
}

//------------------------------------------------------------------------------
void ofxOAuth::setVerifierCallbackServerPort(int portNumber)
{
    vertifierCallbackServerPort = portNumber;
}

//------------------------------------------------------------------------------
void ofxOAuth::setEnableVerifierCallbackServer(bool v)
{
    enableVerifierCallbackServer = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getRequestToken()
{
    return requestToken;
}

//------------------------------------------------------------------------------
void ofxOAuth::setRequestToken(const std::string& v)
{
    requestToken = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getRequestTokenSecret()
{
    return requestTokenSecret;
}

//------------------------------------------------------------------------------
void ofxOAuth::setRequestTokenSecret(const std::string& v)
{
    requestTokenSecret = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getRequestTokenVerifier()
{
    return requestTokenVerifier;
}

//------------------------------------------------------------------------------
void ofxOAuth::setRequestTokenVerifier(const std::string& _requestToken,
                                       const std::string& _requestTokenVerifier)
{
    if(_requestToken == getRequestToken())
    {
        setRequestTokenVerifier(_requestTokenVerifier);
    }
    else
    {
        ofLogError("ofxOAuth::getRequestToken") << "The request token didn't match the request token on record.";
    }
}

//------------------------------------------------------------------------------
void ofxOAuth::setRequestTokenVerifier(const std::string& v)
{
    requestTokenVerifier = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getAccessToken()
{
    return accessToken;
}

//------------------------------------------------------------------------------
void ofxOAuth::setAccessToken(const std::string& v)
{
    accessToken = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getAccessTokenSecret()
{
    return accessTokenSecret;
}

//------------------------------------------------------------------------------
void ofxOAuth::setAccessTokenSecret(const std::string& v)
{
    accessTokenSecret = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getEncodedUserId()
{
    return encodedUserId;
}

//------------------------------------------------------------------------------
void ofxOAuth::setEncodedUserId(const std::string& v)
{
    encodedUserId = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getUserId()
{
    return userId;
}

//------------------------------------------------------------------------------
void ofxOAuth::setUserId(const std::string& v)
{
    userId = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getEncodedUserPassword()
{
    return encodedUserPassword;
}

//------------------------------------------------------------------------------
void ofxOAuth::setEncodedUserPassword(const std::string& v)
{
    encodedUserPassword = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getUserPassword()
{
    return userPassword;
}

//------------------------------------------------------------------------------
void ofxOAuth::setUserPassword(const std::string& v)
{
    userPassword = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getConsumerKey()
{
    return consumerKey;
}

//------------------------------------------------------------------------------
void ofxOAuth::setConsumerKey(const std::string& v)
{
    consumerKey = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getConsumerSecret()
{
    return consumerSecret;
}

//------------------------------------------------------------------------------
void ofxOAuth::setConsumerSecret(const std::string& v)
{
    consumerSecret = v;
}

//------------------------------------------------------------------------------
void ofxOAuth::setApiName(const std::string& v)
{
    apiName = v;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getApiName()
{
    return apiName;
}

//------------------------------------------------------------------------------
void ofxOAuth::receivedVerifierCallbackRequest(const Poco::Net::HTTPServerRequest& request)
{
    ofLogVerbose("ofxOAuth::receivedVerifierCallbackRequest") << "Not implemented.";
    // does nothing with this, but subclasses might.
}

//------------------------------------------------------------------------------
void ofxOAuth::receivedVerifierCallbackHeaders(const Poco::Net::NameValueCollection& headers)
{
    ofLogVerbose("ofxOAuth::receivedVerifierCallbackHeaders") << "Not implemented.";
    // for(NameValueCollection::ConstIterator iter = headers.begin(); iter != headers.end(); iter++) {
    //    ofLogVerbose("ofxOAuth::receivedVerifierCallbackHeaders") << (*iter).first << "=" << (*iter).second;
    //}
    // does nothing with this, but subclasses might.
}

//------------------------------------------------------------------------------
void ofxOAuth::receivedVerifierCallbackCookies(const Poco::Net::NameValueCollection& cookies)
{
    for(Poco::Net::NameValueCollection::ConstIterator iter = cookies.begin();
        iter != cookies.end();
        iter++)
    {
        ofLogVerbose("ofxOAuth::receivedVerifierCallbackCookies") << (*iter).first << "=" << (*iter).second;
    }
    // does nothing with this, but subclasses might.
}

//------------------------------------------------------------------------------
void ofxOAuth::receivedVerifierCallbackGetParams(const Poco::Net::NameValueCollection& getParams)
{
    for(Poco::Net::NameValueCollection::ConstIterator iter = getParams.begin();
        iter != getParams.end();
        iter++) {
        ofLogVerbose("ofxOAuth::receivedVerifierCallbackGetParams") << (*iter).first << "=" << (*iter).second;
    }

    // we normally extract these params
    if(getParams.has("oauth_token") && getParams.has("oauth_verifier"))
    {
        setRequestTokenVerifier(getParams.get("oauth_token"), getParams.get("oauth_verifier"));
    }
    
    // subclasses might also want to extract other get parameters.    
}

//------------------------------------------------------------------------------
void ofxOAuth::receivedVerifierCallbackPostParams(const Poco::Net::NameValueCollection& postParams)
{
    // come soon c++11!
    for(Poco::Net::NameValueCollection::ConstIterator iter = postParams.begin();
        iter != postParams.end();
        iter++)
    {
        ofLogVerbose("ofxOAuth::receivedVerifierCallbackPostParams") << (*iter).first << "=" << (*iter).second;
    }
    
    // does nothing with this, but subclasses might.
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getRealm()
{
    return realm;
}

//------------------------------------------------------------------------------
void ofxOAuth::setRealm(const std::string& v)
{
    realm = v;
}

//------------------------------------------------------------------------------
bool ofxOAuth::isAuthorized()
{
    return !accessToken.empty() && !accessTokenSecret.empty();
}

//------------------------------------------------------------------------------
void ofxOAuth::saveCredentials()
{
    ofxXmlSettings XML;

    XML.getValue("oauth:api_name", apiName);

    XML.setValue("oauth:consumer_key", consumerKey);
    XML.setValue("oauth:consumer_secret", consumerSecret);

    XML.setValue("oauth:access_token", accessToken);

    XML.setValue("oauth:access_secret",accessTokenSecret);
    
    XML.setValue("oauth:screen_name",screenName);
    
    XML.setValue("oauth:user_id", userId);
    XML.setValue("oauth:user_id_encoded",encodedUserId);

    XML.setValue("oauth:user_password", userPassword);
    XML.setValue("oauth:user_password_encoded",encodedUserPassword);

    if(!XML.saveFile(credentialsPathname))
    {
        ofLogError("ofxOAuth::saveCredentials") << "Failed to save : " << credentialsPathname;
    }

}

//------------------------------------------------------------------------------
void ofxOAuth::loadCredentials()
{
    ofxXmlSettings XML;
    
    if(XML.loadFile(credentialsPathname))
    {
//        <oauth api="GENERIC">
//          <consumer_secret></consumer_secret>
//          <consumer_secret></consumer_secret>
//          <access_token></access_token>
//          <access_secret></access_secret>
//          <user_id></user_id>
//          <user_id_encoded></user_id_encoded>
//          <user_password></user_password>
//          <user_password_encoded></user_password_encoded>
//        </oauth>


        if(XML.getValue("oauth:consumer_key","") != consumerKey ||
           XML.getValue("oauth:consumer_secret","") != consumerSecret)
        {
            ofLogError("ofxOAuth::loadCredentials") << "Found a credential file, but did not match the consumer secret / key provided.  Please delete your credentials file: " + ofToDataPath(credentialsPathname) + " and try again.";
            return;
        }


        if(XML.getValue("oauth:access_token", "").empty() ||
           XML.getValue("oauth:access_secret","").empty())
        {
            ofLogError("ofxOAuth::loadCredentials") << "Found a credential file, but access token / secret were empty.  Please delete your credentials file: " + ofToDataPath(credentialsPathname) + " and try again.";
            return;
        }

        apiName             = XML.getValue("oauth:api_name", "");

        accessToken         = XML.getValue("oauth:access_token", "");
        accessTokenSecret   = XML.getValue("oauth:access_secret","");

        screenName          = XML.getValue("oauth:screen_name","");
        
        userId              = XML.getValue("oauth:user_id", "");
        encodedUserId       = XML.getValue("oauth:user_id_encoded","");

        userPassword        = XML.getValue("oauth:user_password", "");
        encodedUserPassword = XML.getValue("oauth:user_password_encoded","");        
    }
    else
    {
        ofLogNotice("ofxOAuth::loadCredentials") << "Unable to locate credentials file: " << ofToDataPath(credentialsPathname);
    }
    
}

//------------------------------------------------------------------------------
void ofxOAuth::setCredentialsPathname(const std::string& credentials)
{
    credentialsPathname = credentials;
}

//------------------------------------------------------------------------------
std::string ofxOAuth::getCredentialsPathname()
{
    return credentialsPathname;
}

//------------------------------------------------------------------------------
void ofxOAuth::resetErrors()
{
    accessFailed = false;
    accessFailedReported = false;
}

//------------------------------------------------------------------------------
ofxOAuth::AuthMethod ofxOAuth::getOAuthMethod()
{
    return oauthMethod;
}

//------------------------------------------------------------------------------
void ofxOAuth::setOAuthMethod(AuthMethod _oauthMethod)
{
    oauthMethod = _oauthMethod;
}

//------------------------------------------------------------------------------
void ofxOAuth::setSSLCACertificateFile(const std::string& pathname)
{
    SSLCACertificateFile = pathname;
    setenv("CURLOPT_CAINFO", ofToDataPath(SSLCACertificateFile).c_str(), true);
}

//------------------------------------------------------------------------------
OAuthMethod ofxOAuth::_getOAuthMethod()
{
    switch (oauthMethod)
    {
        case OFX_OA_HMAC:
            return OA_HMAC;
        case OFX_OA_RSA:
            return OA_RSA;
        case OFX_OA_PLAINTEXT:
            return OA_PLAINTEXT;
        default:
            ofLogError("ofxOAuth::_getOAuthMethod") << "Unknown OAuthMethod, defaulting to OA_HMAC. oauthMethod=" << oauthMethod;
            return OA_HMAC;
    }
}

//------------------------------------------------------------------------------
std::string ofxOAuth::_getHttpMethod()
{
    switch (httpMethod)
    {
        case OFX_HTTP_GET:
            return "GET";
        case OFX_HTTP_POST:
            return "POST";
        default:
            ofLogError("ofxOAuth::_getHttpMethod") << "Unknown HttpMethod, defaulting to GET. httpMethod=" << httpMethod;
            return "GET";
    }
}

//------------------------------------------------------------------------------
std::string ofxOAuth::appendQuestionMark(const std::string& url) const
{
    std::string u = url;
    if(!u.empty() && u.substr(u.size()-1,u.size()-1) != "?") u += "?"; // need that
    return u;
}
