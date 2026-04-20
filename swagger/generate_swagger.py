import json
import re

def parse_markdown(md_text):
    api_docs = {}
    
    # regex patterns
    h3_pattern = re.compile(r'^###\s+(.+)$')
    h4_pattern = re.compile(r'^####\s+[\d\.]+\s+(.+)$')
    url_pattern = re.compile(r'-\s+\*\*URL\*\*:\s+`([^`]+)`')
    method_pattern = re.compile(r'-\s+\*\*Method\*\*:\s+`([A-Z]+)`')
    param_pattern = re.compile(r'\s+-\s+`([^`]+)`\s+\(([^)]+)\):\s+(.+)')
    desc_pattern = re.compile(r'-\s+\*\*Description\*\*:\s+(.+)')
    header_pattern = re.compile(r'\s+-\s+`([^`]+)`:\s*(.+)')
    body_pattern = re.compile(r'-\s+\*\*Body\*\*:\s*```json\n(.*?)\n```', re.DOTALL)
    response_pattern = re.compile(r'-\s+\*\*Response.*?\*\*:\s*```json\n(.*?)\n```', re.DOTALL)
    
    current_api = None
    current_title = None
    lines = md_text.split('\n')
    
    i = 0
    while i < len(lines):
        line = lines[i]
        
        # Match H4 (or H3 if it's an API block without H4)
        m_h4 = h4_pattern.match(line)
        m_h3 = h3_pattern.match(line)
        
        if m_h4 or (m_h3 and current_api is None and i+1 < len(lines) and lines[i+1].startswith('- **URL**:')):
            if current_api and 'url' in current_api:
                api_docs[current_api['url']] = current_api
                
            current_title = m_h4.group(1) if m_h4 else m_h3.group(1)
            current_api = {
                'title': current_title.strip(),
                'params': [],
                'headers': [],
                'description': '',
                'body': None,
                'response': None
            }
            i += 1
            continue
            
        if current_api:
            m_url = url_pattern.match(line)
            if m_url:
                url_full = m_url.group(1)
                # extract base path from url like /user/login/pwd?a=1
                url_base = url_full.split('?')[0]
                current_api['url'] = url_base
                current_api['full_url'] = url_full
                
            m_method = method_pattern.match(line)
            if m_method:
                current_api['method'] = m_method.group(1).lower()
                
            m_desc = desc_pattern.match(line)
            if m_desc:
                current_api['description'] = m_desc.group(1)
                
            if line.startswith('- **Params**:'):
                i += 1
                while i < len(lines) and lines[i].startswith('  -'):
                    m_p = param_pattern.match(lines[i])
                    if m_p:
                        name, attrs, desc = m_p.groups()
                        required = 'required' in attrs.lower()
                        current_api['params'].append({
                            'name': name,
                            'required': required,
                            'description': desc
                        })
                    i += 1
                continue
                
            if line.startswith('- **Headers**:'):
                i += 1
                while i < len(lines) and lines[i].startswith('  -'):
                    m_h = header_pattern.match(lines[i])
                    if m_h:
                        name, desc = m_h.groups()
                        current_api['headers'].append({
                            'name': name,
                            'description': desc
                        })
                    i += 1
                continue
            
            # Extract Body
            if line.startswith('- **Body**:'):
                body_block = []
                i += 2 # skip - **Body**: and ```json
                while i < len(lines) and not lines[i].startswith('  ```') and not lines[i].startswith('```'):
                    body_block.append(lines[i])
                    i += 1
                current_api['body'] = '\n'.join(body_block)
                continue
                
            # Extract Response
            if line.startswith('- **Response') and '```json' in lines[i+1]:
                resp_block = []
                i += 2
                while i < len(lines) and not lines[i].startswith('  ```') and not lines[i].startswith('```'):
                    resp_block.append(lines[i])
                    i += 1
                current_api['response'] = '\n'.join(resp_block)
                continue

        i += 1
        
    if current_api and 'url' in current_api:
        api_docs[current_api['url']] = current_api
        
    return api_docs

def generate_swagger(api_docs):
    swagger = {
      "openapi": "3.0.0",
      "info": {
        "title": "UEAdminAPI",
        "description": "API documentation for UEAdminAPI Drogon Backend.\nThis document is generated based on `API Reference Documentation.md`.",
        "version": "1.0.0"
      },
      "servers": [
        {"url": "http://localhost:5555", "description": "Local server"},
        {"url": "https://im.uesoft.com", "description": "Remote server"}
      ],
      "paths": {},
      "components": {
        "securitySchemes": {
          "bearerAuth": {
            "type": "http",
            "scheme": "bearer",
            "bearerFormat": "JWT"
          },
          "FlashToken": {
            "type": "apiKey",
            "in": "header",
            "name": "AuthorizationFlashToken",
            "description": "FlashToken for login"
          },
          "ActionToken": {
            "type": "apiKey",
            "in": "header",
            "name": "X-Action-Token",
            "description": "Action Token obtained from /user/action_token"
          }
        }
      }
    }

    # Group by tags based on URL prefix
    def get_tag(url):
        if url.startswith('/api/gitlab'): return 'GitLab'
        if url.startswith('/api/third'): return 'ThirdPartyLogin'
        if url.startswith('/user/login'): return 'Login'
        if url.startswith('/user/mfa'): return 'VerificationCode'
        if url.startswith('/user'): return 'User Management'
        if url.startswith('/system'): return 'System'
        return 'Default'

    for url, api in api_docs.items():
        if url not in swagger['paths']:
            swagger['paths'][url] = {}
            
        method = api.get('method', 'get')
        
        op = {
            "tags": [get_tag(url)],
            "summary": api['title'],
            "description": api['description'],
            "parameters": [],
            "responses": {
                "200": {
                    "description": "Successful response"
                }
            }
        }
        
        # Add response example if exists
        if api.get('response'):
            try:
                resp_json = json.loads(api['response'])
                op['responses']['200']['content'] = {
                    "application/json": {
                        "example": resp_json
                    }
                }
            except:
                pass
                
        # Add body example if exists
        if api.get('body'):
            # try to parse as json, even if it contains comments
            clean_body = re.sub(r'//.*$', '', api['body'], flags=re.MULTILINE)
            try:
                body_json = json.loads(clean_body)
                op['requestBody'] = {
                    "required": True,
                    "content": {
                        "application/json": {
                            "example": body_json
                        }
                    }
                }
            except Exception as e:
                # Fallback to plain string example
                op['requestBody'] = {
                    "required": True,
                    "content": {
                        "application/json": {
                            "schema": {"type": "object", "description": "See example for structure"},
                            "example": api['body']
                        }
                    }
                }
                
        # Add path/query parameters
        # Also parse URL path variables (e.g. {platform})
        path_vars = re.findall(r'\{([^}]+)\}', url)
        for pv in path_vars:
            op['parameters'].append({
                "name": pv,
                "in": "path",
                "required": True,
                "schema": {"type": "string"}
            })
            
        for param in api['params']:
            if param['name'] in path_vars:
                continue # Already added as path param
            op['parameters'].append({
                "name": param['name'],
                "in": "query",
                "required": param['required'],
                "description": param['description'],
                "schema": {"type": "string"}
            })
            
        # Add Security Headers
        security = []
        for header in api['headers']:
            h_name = header['name'].lower()
            if 'authorization' in h_name and 'flashtoken' not in h_name:
                security.append({"bearerAuth": []})
            elif 'x-action-token' in h_name:
                op['parameters'].append({
                    "name": "X-Action-Token",
                    "in": "header",
                    "required": True,
                    "description": header['description'],
                    "schema": {"type": "string"}
                })
            elif 'authorizationflashtoken' in h_name:
                op['parameters'].append({
                    "name": "AuthorizationFlashToken",
                    "in": "header",
                    "required": True,
                    "schema": {"type": "string"}
                })
                
        if security:
            op['security'] = security
            
        swagger['paths'][url][method] = op

    return swagger

if __name__ == '__main__':
    with open("d:/vc/UEAdminAPI_drogon/docs/API Reference Documentation.md", "r", encoding="utf-8") as f:
        md_text = f.read()
        
    api_docs = parse_markdown(md_text)
    swagger_json = generate_swagger(api_docs)
    
    with open("d:/vc/UEAdminAPI_drogon/swagger/swagger.json", "w", encoding="utf-8") as f:
        json.dump(swagger_json, f, indent=2, ensure_ascii=False)
        
    print(f"swagger.json generated successfully with {len(api_docs)} APIs.")
