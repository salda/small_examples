#include "VerifyPlayerExistenceAndAdulthoodFunction.hpp"
#include "betting_codes.hpp"
#include "betting_session.hpp"
#include "betting_request_handler.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

#include <curl/curl.h>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

using namespace xercesc;

size_t CurlWrite_CallbackFunc_StdString(void *contents, size_t size, size_t nmemb, std::string * response) {
	size_t newLength = size*nmemb;
	size_t oldLength = response->size();
	try {
		response->resize(oldLength + newLength);
	}
	catch (std::bad_alloc &e) {
		//handle memory problem
		return 0;
	}
	std::copy((char*)contents, (char*)contents + newLength, response->begin() + oldLength);
	return newLength;
}

RESPONSE_CODE searchCUZK(CURL * curl, const std::string & queryURL, std::string & response) {
	response.clear();
	curl_easy_setopt(curl, CURLOPT_URL, queryURL.c_str());
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		JOURNAL(J_ERROR, std::string(curl_easy_strerror(res)).c_str());
		return C_CUZK_CURL_ERROR;
	}
	long respCode;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respCode);
	if (respCode != 200 || response.empty()) {
		JOURNAL(J_ERROR, respCode != 200 ? ("response code from vdp.cuzk.cz: " + std::to_string(respCode)).c_str() : "no response from vdp.cuzk.cz");
		return respCode != 200 ? C_CUZK_HTTP_CODE_NOT_200 : C_CUZK_NO_RESPONSE;
	}
	return C_NONE;
}

RESPONSE_CODE VerifyPlayerExistenceAndAdulthoodFunction::call(std::string & Jmeno, std::string & Prijmeni, std::string & NarozeniDatum, 
	std::string & Ulice, std::string & PopisneCislo, std::string & Mesto, std::string & KodZIP, std::string & StatKod, std::string & SvetText)
{
	RESPONSE_CODE result = C_NONE;
	CURL * curl = curl_easy_init();
	if (!curl) {
		JOURNAL(J_ERROR, "curl not initialized coorectly");
		return C_CURL_INITIALIZATION_ERROR;
	}
	std::vector<unsigned int> TrvaleBydlisteAdresniMistoKods;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
	if (StatKod == "203") {
		std::string response;
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		std::vector<unsigned int> municipalities;
		char * escapedMesto = curl_easy_escape(curl, Mesto.c_str(), 0);
		result = searchCUZK(curl, "vdp.cuzk.cz/vdp/ruian/obce/vyhledej?ob.nazev=" + std::string(escapedMesto) + "&search=Vyhledat", response);
		if (escapedMesto)
			curl_free(escapedMesto);
		/*if (!response.empty()) {//uncomment for saving the response to a file
			std::ofstream myfile;
			myfile.open("obce.xml");
			myfile << response;
			myfile.close();
		}*/
		if (!result) {
			size_t kodValuePos = 0;
			while ((kodValuePos = response.find(municipalities.size() % 2 ? "<tr class=\"e\"><td>" : "<tr class=\"o\"><td>", kodValuePos + 1)) != std::string::npos) {//find all municipalities
				kodValuePos += 18;
				municipalities.push_back(std::stoi(response.substr(kodValuePos, response.find("</td>", kodValuePos) - kodValuePos)));
			}
			if (!municipalities.size()) {
				JOURNAL(J_ERROR, ("municipality " + Mesto + " not found").c_str());
				result = C_CUZK_MUNICIPALITY_NOT_FOUND;
			}
			else {
				std::multimap<unsigned int, unsigned int> streetsOrPartsOfMunicipality;
				for (unsigned int municipality : municipalities) {
					if (Ulice.empty())
						result = searchCUZK(curl, "vdp.cuzk.cz/vdp/ruian/castiobce/vyhledej?ob.kod=" + std::to_string(municipality) + "&search=Vyhledat", response);
					else {
						char * escapedUlice = curl_easy_escape(curl, Ulice.c_str(), 0);
						result = searchCUZK(curl, "vdp.cuzk.cz/vdp/ruian/ulice/vyhledej?ob.kod=" + std::to_string(municipality) + "&ul.nazev=" + std::string(escapedUlice) + "&search=Vyhledat", response);
						if (escapedUlice)
							curl_free(escapedUlice);
					}
					/*if (!response.empty()) {//uncomment for saving the response to a file
						static int uliceACastiObciIndex = 1;
						std::ofstream myfile;
						myfile.open("uliceACastiObci" + std::to_string(uliceACastiObciIndex) + ".xml");
						myfile << response;
						myfile.close();
						++uliceACastiObciIndex;
					}*/
					if (!result) {
						kodValuePos = 0;
						bool isOdd = false;
						while ((kodValuePos = response.find(isOdd ? "<tr class=\"e\"><td>" : "<tr class=\"o\"><td>", kodValuePos + 1)) != std::string::npos) {//find all streets or parts of municipality
							isOdd = !isOdd;
							kodValuePos += 18;
							streetsOrPartsOfMunicipality.emplace_hint(streetsOrPartsOfMunicipality.end(), municipality, std::stoi(response.substr(kodValuePos, response.find("</td>", kodValuePos) - kodValuePos)));
						}
					}
				}
				if (!streetsOrPartsOfMunicipality.size()) {
					JOURNAL(J_ERROR, (Ulice.empty() ? "street not specified and " + Mesto + " has no parts (this error should never happen, fix in code needed)"
						: "street " + Ulice + " not found").c_str());
					result = C_CUZK_STREET_NOT_FOUND;
				}
				else {
					for (std::pair<unsigned int, unsigned int> streetOrPartOfMunicipality : streetsOrPartsOfMunicipality) {
						result = searchCUZK(curl, "vdp.cuzk.cz/vdp/ruian/adresnimista/vyhledej?ob.kod=" + std::to_string(streetOrPartOfMunicipality.first)
							+ (Ulice.empty() ? "&co.kod=" : "&ul.kod=") + std::to_string(streetOrPartOfMunicipality.second) + 
							"&so.cisDom=" + PopisneCislo + "&so.typKod=1&ad.psc=" + KodZIP + "&search=Vyhledat", response);
						/*if (!response.empty()) {//uncomment for saving the response to a file
							static int kodyAdresIndex = 1;
							std::ofstream myfile;
							myfile.open("kodyAdres" + std::to_string(kodyAdresIndex) + ".xml");
							myfile << response;
							myfile.close();
							++kodyAdresIndex;
						}*/
						if (!result) {
							kodValuePos = 0;
							bool isOdd = false;
							while ((kodValuePos = response.find(isOdd ? "<tr class=\"e\"><td>" : "<tr class=\"o\"><td>", kodValuePos + 1)) != std::string::npos) {//find all address codes
								isOdd = !isOdd;
								kodValuePos += 18;
								TrvaleBydlisteAdresniMistoKods.push_back(std::stoi(response.substr(kodValuePos, response.find("</td>", kodValuePos) - kodValuePos)));
							}
						}
						if (!TrvaleBydlisteAdresniMistoKods.size()) {
							JOURNAL(J_ERROR, ("address " + Ulice + " " + PopisneCislo + ", " + Mesto + ", " + KodZIP + " not found").c_str());
							result = C_CUZK_ADDRESS_NOT_FOUND;
						}
					}
				}
			}
		}
	}
	if (result != C_NONE)
		curl_easy_cleanup(curl);
	else {
		curl_easy_setopt(curl, CURLOPT_URL, "https://sdsl.mfcr.cz/app/SdslOverHrace/v100/Service.svc");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
		struct curl_slist * curlLstHeaders = NULL;
		curlLstHeaders = curl_slist_append(curlLstHeaders, "Content-Type: application/soap+xml; charset=utf-8");
		curlLstHeaders = curl_slist_append(curlLstHeaders, "Connection: Keep-Alive");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlLstHeaders);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		std::string response;
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		time_t tm_t = time(NULL) - 60;
		tm * ptm = gmtime(&tm_t);
		char timeNow[82];
		strftime(timeNow, 82, "%Y-%m-%dT%H:%M:%S.000Z", ptm);
		tm_t += 5 * 60;
		ptm = gmtime(&tm_t);
		char timeIn5minutes[82];
		strftime(timeIn5minutes, 82, "%Y-%m-%dT%H:%M:%S.000Z", ptm);
		char request[8092];
		std::string address;
		for (int i = 0; i != (StatKod == "203" ? TrvaleBydlisteAdresniMistoKods.size() : 1); ++i) {
			if (StatKod == "203")
				address = "<TrvaleBydlisteAdresniMistoKod>" + std::to_string(TrvaleBydlisteAdresniMistoKods[i]) + "</TrvaleBydlisteAdresniMistoKod>";
			else
				address =
				"<NarozeniMisto>" \
					"<StatKod>" + StatKod + "</StatKod>" +
					"<SvetText>" + SvetText + "</SvetText>" +
				"</NarozeniMisto>";
			sprintf(request, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
				"<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://www.w3.org/2005/08/addressing\" xmlns:u=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
					"<s:Header>"
						"<a:Action s:mustUnderstand=\"1\">urn:cz:mfcr:sdsl:ws:OverHrace:v1:Dotaz</a:Action>"
						"<a:MessageID>urn:uuid:", boost::uuids::to_string(boost::uuids::random_generator()()).c_str(), "</a:MessageID>"
						"<a:ReplyTo>"
							"<a:Address>http://www.w3.org/2005/08/addressing/anonymous</a:Address>"
						"</a:ReplyTo>"
						"<a:To s:mustUnderstand=\"1\">https://sdsl.mfcr.cz/app/SdslOverHrace/v100/Service.svc</a:To>"
						"<o:Security s:mustUnderstand=\"1\" xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
							"<u:Timestamp u:Id=\"_0\">"
								"<u:Created>", timeNow, "</u:Created>"
								"<u:Expires>", timeIn5minutes, "</u:Expires>"
							"</u:Timestamp>"
							"<o:UsernameToken u:Id=\"<secret>\">"
								"<o:Username><secret></o:Username>"
								"<o:Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordText\"><secret></o:Password>"
							"</o:UsernameToken>"
						"</o:Security>"
					"</s:Header>"
					"<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
						"<OverHraceDotaz xmlns=\"urn:cz:mfcr:sdsl:schemas:OverHrace:Request:v1\">"
							"<Jmeno>", Jmeno.c_str(), "</Jmeno>"
							"<Prijmeni>", Prijmeni.c_str(), "</Prijmeni>"
							"<NarozeniDatum>", NarozeniDatum.c_str(), "</NarozeniDatum>"
							, address.c_str(),
						"</OverHraceDotaz>"
					"</s:Body>"
				"</s:Envelope>");
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
			CURLcode res = curl_easy_perform(curl);
			//printf("Request body part1: %.*s\n", 1024, request);//uncomment for getting the request
			//printf("Request body part2: %.*s\n", 1024, request + 1024);//uncomment for getting the request
			if (res != CURLE_OK) {
				std::string error(curl_easy_strerror(res));
				JOURNAL(J_ERROR, error.c_str());
				result = C_ROB_CURL_ERROR;
				break;
			}
			else {
				long respCode;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respCode);
				//if (!response.empty())//uncomment for getting the response
					//std::cout << "Response body: " << response << std::endl;
				if (respCode == 200 && !response.empty()) {
					size_t HracOvereniVRob_position = response.find("<HracOvereniVRob>") + 17;
					size_t HracPlnoletost_position = response.find("<HracPlnoletost>") + 16;
					if (HracOvereniVRob_position == std::string::npos || response[HracOvereniVRob_position] != '1')
						result = C_ROB_PERSON_NOT_EXISTENT;
					else if (HracPlnoletost_position == std::string::npos || response[HracPlnoletost_position] != '1')
						result = C_ROB_PERSON_NOT_ADULT;
					else {
						result = C_NONE;
						break;
					}
				}
				else {
					if (!response.empty()) {
						size_t reasonPos = response.find("<s:Reason><s:Text xml:lang=\"cs-CZ\">");
						if (reasonPos != std::string::npos) {
							JOURNAL(J_ERROR, ("response code from sdsl.mfcr.cz: " + std::to_string(respCode) + ", response text: " +
								response.substr(reasonPos + 35, response.find("</s:Text>", reasonPos) - reasonPos - 35)).c_str());
						}
						else
							JOURNAL(J_ERROR, ("response code from sdsl.mfcr.cz: " + std::to_string(respCode)).c_str());
					}
					else
						JOURNAL(J_ERROR, "no response from sdsl.mfcr.cz");
					result = respCode != 200 ? C_ROB_HTTP_CODE_NOT_200 : C_ROB_NO_RESPONSE;
					break;
				}
			}
		}
		curl_slist_free_all(curlLstHeaders);
		curl_easy_cleanup(curl);
	}
	return result;
}