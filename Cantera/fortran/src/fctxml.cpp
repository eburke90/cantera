// Build as a DLL under Windows
#ifdef WIN32
#define DLL_EXPORT __declspec(dllexport)
#pragma warning(disable:4786)
#pragma warning(disable:4503)
#else
#define DLL_EXPORT
#endif

// Cantera includes
#include "ctml.h"
#include "importCTML.h"

#include "../../clib/src/Cabinet.h"
//#include "Storage.h"


// Values returned for error conditions
#define ERR -999
#define DERR -999.999

Cabinet<XML_Node>*   Cabinet<XML_Node>::__storage = 0;

inline XML_Node* _xml(const integer* i) {
    return Cabinet<XML_Node>::cabinet(false)->item(*i);
}

static void handleError() {
    error(lastErrorMessage());
}

extern "C" {  

    int DLL_EXPORT fxml_new_(const char* name, ftnlen namelen) {
        XML_Node* x;
        if (!name) 
            x = new XML_Node;
        else 
            x = new XML_Node(string(name, namelen));
        return Cabinet<XML_Node>::cabinet(true)->add(x);
    }

    int DLL_EXPORT fxml_get_xml_file_(const char* file, ftnlen filelen) {
        //try {
            XML_Node* x = get_XML_File(string(file, filelen));
            int ix = Cabinet<XML_Node>::cabinet(false)->add(x);
            cout << "ix = " << ix << endl;
            return ix;
            //}
            //catch (CanteraError) {
            //cout << "error...." << endl; return 23; }
    }

    int DLL_EXPORT fxml_clear_() {
        try {
            Cabinet<XML_Node>::cabinet(false)->clear();
            close_XML_File("all");
            return 0;
        }
        catch (CanteraError) { return -1; }
    }

    int DLL_EXPORT fxml_del_(const integer* i) {
        Cabinet<XML_Node>::cabinet(false)->del(*i);
        return 0;
    }

    int DLL_EXPORT fxml_removechild_(const integer* i, const integer* j) {
        _xml(i)->removeChild(_xml(j));
        return 0;
    }

    int DLL_EXPORT fxml_copy_(const integer* i) {
        return Cabinet<XML_Node>::cabinet(false)->newCopy(*i);
    }

    int DLL_EXPORT fxml_assign_(const integer* i, const integer* j) {
        return Cabinet<XML_Node>::cabinet(false)->assign(*i,*j);
    }

    int DLL_EXPORT fxml_preprocess_and_build_(const integer* i, 
        const char* file, ftnlen filelen) {
        try {
            get_CTML_Tree(_xml(i), string(file, filelen));
            return 0;
        }
        catch (CanteraError) { return -1; }
    }



    int DLL_EXPORT fxml_attrib_(const integer* i, const char* key, 
        char* value, ftnlen keylen, ftnlen valuelen) {
        try {
            string ky = string(key, keylen);
            XML_Node& node = *_xml(i);
            if (node.hasAttrib(ky)) {
                string v = node[ky];
                strncpy(value, v.c_str(), valuelen);
            }
            else 
                throw CanteraError("fxml_attrib","node "
                    " has no attribute '"+ky+"'");
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_addattrib_(const integer* i, 
        const char* key, const char* value, ftnlen keylen, ftnlen valuelen) {
        try {
            string ky = string(key, keylen);
            string val = string(value, valuelen);
            XML_Node& node = *_xml(i);
            node.addAttribute(ky, val);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_addcomment_(const integer* i, const char* comment, 
        ftnlen commentlen) {
        try {
            string c = string(comment, commentlen);
            XML_Node& node = *_xml(i);
            node.addComment(c);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_tag_(const integer* i, char* tag, ftnlen taglen) {
        try {
            XML_Node& node = *_xml(i);
            const string v = node.name();
            strncpy(tag, v.c_str(), taglen);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_value_(const integer* i, char* value, ftnlen valuelen) {
        try {
            XML_Node& node = *_xml(i);
            const string v = node.value();
            strncpy(value, v.c_str(), valuelen);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_child_(const integer* i, const char* loc, ftnlen loclen) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node& c = node.child(string(loc, loclen));
            return Cabinet<XML_Node>::cabinet()->add(&c);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_child_bynumber_(const integer* i, const integer* m) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node& c = node.child(*m);
            return Cabinet<XML_Node>::cabinet()->add(&c);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_findid_(const integer* i, const char* id, ftnlen idlen) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node* c = node.findID(string(id, idlen));
            if (c) {
                return Cabinet<XML_Node>::cabinet()->add(c);
            }
            else 
                throw CanteraError("fxml_find_id","id not found: "+string(id, idlen));
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_findbyname_(const integer* i, const char* nm, ftnlen nmlen) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node* c = node.findByName(string(nm, nmlen));
            if (c) {
                return Cabinet<XML_Node>::cabinet()->add(c);
            }
            else 
                throw CanteraError("fxml_findByName","name "+string(nm, nmlen)
                    +" not found");
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_nchildren_(const integer* i) {
        try {
            XML_Node& node = *_xml(i);
            return node.nChildren();
        }
        catch (CanteraError) { return -1; }
    }

    int DLL_EXPORT fxml_addchild_(const integer* i, const char* name, 
        const char* value, ftnlen namelen, ftnlen valuelen) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node& c = node.addChild(string(name, namelen),
                string(value,valuelen));
            return Cabinet<XML_Node>::cabinet()->add(&c);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_addchildnode_(const integer* i, const integer* j) {
        try {
            XML_Node& node = *_xml(i);
            XML_Node& chld = *_xml(j);
            XML_Node& c = node.addChild(chld);
            return Cabinet<XML_Node>::cabinet()->add(&c);
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT fxml_write_(const integer* i, const char* file, ftnlen filelen) {
        try {
            string ff(file, filelen);
            ofstream f(ff.c_str());
            if (f) {
                XML_Node& node = *_xml(i);
                node.write(f);
            }
            else {
                throw CanteraError("fxml_write",
                    "file "+string(file, filelen)+" not found.");
            }
            return 0;
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

    int DLL_EXPORT ctml_getfloatarray_(const integer* i, const integer* n, 
        doublereal* data, const integer* iconvert) {
        try {
            XML_Node& node = *_xml(i);
            vector_fp v;
            bool conv = false;
            if (*iconvert > 0) conv = true;
            getFloatArray(node, v, conv);
            int nv = v.size();

            // array not big enough
            if (*n < nv) {
                throw CanteraError("ctml_getfloatarray",
                    "array must be dimensioned at least "+int2str(nv));
            }
            
            for (int i = 0; i < nv; i++) {
                data[i] = v[i];
            }
            //n = nv;
        }
        catch (CanteraError) { return -1; }
        return 0;
    }

}
