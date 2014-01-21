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


#pragma once


#include "ofxOAuth.h"


class ofxExampleTwitterClient: public ofxOAuth
{
public:
    ofxExampleTwitterClient()
    {
    }

    virtual ~ofxExampleTwitterClient()
    {
    }
    
    void setup(const std::string& consumerKey,
               const std::string& consumerSecret)
    {
        ofxOAuth::setup("https://api.twitter.com",
                        consumerKey,
                        consumerSecret);
    }

    // Once setup is called, authenticated calls can be made.
    // This method is just an example of whatyour calls might look like.
    std::string exampleMethod()
    {
        return get("/1.1/statuses/retweets_of_me.json");
    }
	
	// Returns the 20 most recent mentions (tweets containing a users's @screen_name) for the authenticating user
	// The timeline returned is the equivalent of the one seen when you view your mentions on twitter.com.
	// This method can only return up to 800 tweets.
	// https://dev.twitter.com/docs/api/1.1/get/statuses/mentions_timeline
	std::string getMentions(){
		
		return get("/1.1/statuses/mentions_timeline.json");
	}
	
	

};
