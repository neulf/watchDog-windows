#include "watchDogService.h"
#define LOG_TAG L"watchDogService"
#include "Logger.h"
#include "base64.h"



SERVICE_STATUS                      ServiceStatus;                              //����״̬
SERVICE_STATUS_HANDLE               hStatus;                                    //����״̬���

//----------------


PROCESS_INFORMATION					pi;											//�ӽ��̾��
DWORD								returnCode;									//�ӽ��̷�����
STARTUPINFO							si = { sizeof(STARTUPINFO) };

//---------------
HANDLE								hToken;										//�û�token
HANDLE								hTokenDup;									//�û�token
LPVOID								pEnv;										//������Ϣ

#define MAX_RETRIES 10          // ���Ի�ȡ���Ƶ�������
#define RETRY_DELAY_MS 2000    // ÿ�γ���֮����ӳ٣����룩

//���Ź���������
void WINAPI ServiceMain(DWORD argc, PWSTR* argv) {

	ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
	ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
	ServiceStatus.dwWin32ExitCode = NO_ERROR;
	ServiceStatus.dwServiceSpecificExitCode = NO_ERROR;
	ServiceStatus.dwCheckPoint = 0;
	ServiceStatus.dwWaitHint = 0;

	hStatus = RegisterServiceCtrlHandler(ServiceName, ServiceCtrlHandler);
	if (!hStatus)
	{
		DWORD dwError = GetLastError();
		log_e(_T("��������ʧ��!%d\n"), dwError);
		return;
	}

	//���÷���״̬
	ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	SetServiceStatus(hStatus, &ServiceStatus);
	
	Run();

	//ֹͣ����
	ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
	SetServiceStatus(hStatus, &ServiceStatus);
}


//����ص�
void WINAPI ServiceCtrlHandler(DWORD fdwControl)
{
	switch (fdwControl) {
	case SERVICE_CONTROL_STOP:
		log_i(_T("WatchDog ����ֹͣ...\n"));
		ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		ServiceStatus.dwWin32ExitCode = 0;
		SetServiceStatus(hStatus, &ServiceStatus);
		break;
	case SERVICE_CONTROL_SHUTDOWN:
		log_i(_T("WatchDog ������ֹ...\n"));
		ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		ServiceStatus.dwWin32ExitCode = 0;
		SetServiceStatus(hStatus, &ServiceStatus);
		break;
	default:
		break;
	}
}

//����ָ������
void Run() {
	log_i(_T("������óɹ�!\n"));
	DogFood * dogFood = CreateDogFood();
	if (dogFood) {
		wchar_t * commandLine = ParseConfForCmd();
		BOOL flag = createProcess(commandLine);
		if (flag) {
			watching(dogFood, commandLine);
		}
		else {
			log_e(_T("��������ʧ��!\n"));
		}

		free(commandLine);
	}
	else {
		log_e(_T("��������ʧ��!\n"));
		
	}
	log_i(_T("ֹͣ������....\n"));
	
}

//���������ļ�
wchar_t * ParseConfForCmd() {
	TCHAR * path = GetFullDir();
	TCHAR * configFilePath = _tcscat(path, _T(CONFIG_FILE_PATH));
	//�ļ��ṹ��
	FILE * config_file;
	//���ļ�
	if ((config_file = _tfopen(configFilePath, _T("r"))) == NULL) {
		log_e(_T("�����ļ���ʧ��!\n"));
		exit(EXIT_FAILURE);
	}

	//��ȡ�ļ�����
	//��λ�ļ�ƫ�Ƶ�ĩβ
	fseek(config_file, 0L, SEEK_END);
	//��ȡ�ļ�����
	long total_size = ftell(config_file);
	if (total_size < 0) {
		log_e(_T("��ȡ�����ļ�ʧ��!�����ļ�����Ϊ0\n"));
		exit(EXIT_FAILURE);
	}
	//�����ڴ�
	char * json_data = malloc(sizeof(char) * total_size + 1);

	if (json_data == NULL) {
		log_e(_T("��ȡ�����ļ�ʧ��!�����ڴ�ʧ��\n"));
		exit(EXIT_FAILURE);
	}

	//�����ļ�ָ�뵽��ͷ

	fseek(config_file, 0L, SEEK_SET);

	fread(json_data, sizeof(char), total_size, config_file);

	//�ر������ļ�
	fclose(config_file);

	//����json
	cJSON *json = cJSON_Parse(json_data);

	cJSON *cmd = cJSON_GetObjectItem(json, "cmd");

	if (cmd == NULL) {
		log_e(_T("�޷���������,��������Ч!��������cmd����\n"));
		exit(EXIT_FAILURE);
	}

	//ת���ַ�
	char * base64CmdStr = cmd->valuestring;
	//ת��
	char * cmdStr =  base64_decode(base64CmdStr);

	wchar_t * commandLine = CharToWchar(cmdStr);
	if (commandLine == NULL) {
		log_e(_T("�޷���������,��������Ч!ת���ֽ���Ч\n"));
		exit(EXIT_FAILURE);
	}

	//�ͷ�JSON�ַ����ڴ�
	free(json_data);
	free(configFilePath);
	free(cmdStr);
	return commandLine;
}

BOOL CreateProcessNoService(const wchar_t * commandLine) {
	return  CreateProcess(NULL, commandLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
}

//��ȡuser token������Զ������ִ�е����
BOOL GetUserTokenForRDP(HANDLE* phToken) {
	WTS_SESSION_INFO* pSessionInfo = NULL;
	DWORD dwCount = 0;

	if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &dwCount)) {
		for (DWORD i = 0; i < dwCount; ++i) {
			if (pSessionInfo[i].State == WTSActive) { // �ҵ���Ự
				DWORD dwSessionId = pSessionInfo[i].SessionId;
				log_i(_T("�ҵ���ỰID: %u\n"), dwSessionId);

				if (WTSQueryUserToken(dwSessionId, phToken)) {
					log_i(_T("�ɹ���ȡ�û����ơ�hToken: %p\n"), *phToken);
					WTSFreeMemory(pSessionInfo);
					return TRUE;
				}
				else {
					DWORD dwError = GetLastError();
					log_e(_T("WTSQueryUserToken ʧ�ܣ�������: %d\n"), dwError);
				}
			}
		}
		WTSFreeMemory(pSessionInfo);
	}
	else {
		log_e(_T("WTSEnumerateSessions ʧ��\n"));
	}

	return FALSE;
}

//���񻷾��´�������
BOOL CreateProcessForService(const wchar_t * commandLine) {

	DWORD dwSessionID = WTSGetActiveConsoleSessionId();

	if (dwSessionID == 0xFFFFFFFF) {
		log_e(_T("��ǰû�л���û��Ự\n"));
		return FALSE;
	}
	log_i(_T("��ǰ��ỰID: %u\n"), dwSessionID);


	for (int i = 0; i < MAX_RETRIES; i++) {
		//��ȡ��ǰ���ڻ״̬�û���Token
		if (!WTSQueryUserToken(dwSessionID, &hToken)) {
			int nCode = GetLastError();
			log_e(_T("��ȡ�û�tokenʧ��,������:%d\n"), nCode);

			if (i < (MAX_RETRIES - 1)) {
				_tprintf(_T("�ȴ� %d ���������...\n"), RETRY_DELAY_MS);
				Sleep(RETRY_DELAY_MS);  // �ȴ�һ��ʱ�������

				if (GetUserTokenForRDP(&hToken))
				{
					break;
				}
			}
			else
			{
				CloseHandle(hToken);
				return FALSE;
			}
		}
		else
		{
			break;
		}
	}

	//�����µ�Token
	if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hTokenDup)) {
		int nCode = GetLastError();
		log_e(_T("�����û�tokenʧ��,������:%d\n"), nCode);

		CloseHandle(hToken);
		return FALSE;
	}

	//����������Ϣ
	if (!CreateEnvironmentBlock(&pEnv, hTokenDup, FALSE)) {
		DWORD nCode = GetLastError();
		log_e(_T("����������Ϣʧ��,������:%d\n"), nCode);
		CloseHandle(hTokenDup);
		CloseHandle(hToken);
		return FALSE;
	}

	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.lpDesktop = _T("winsta0\\default");

	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

	//��ʼ��������
	DWORD dwCreateFlag = NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;


	if (!CreateProcessAsUser(hTokenDup, NULL, commandLine, NULL, NULL, FALSE, dwCreateFlag, pEnv, NULL, &si, &pi))
	{
		DWORD nCode = GetLastError();
		log_e(_T("��������ʧ��,������:%d\n"), nCode);
		DestroyEnvironmentBlock(pEnv);
		CloseHandle(hTokenDup);
		CloseHandle(hToken);
		return FALSE;
	}
	//SetPrivilege(hToken, SE_CREATE_GLOBAL_NAME, TRUE);

	//����һ������
	return TRUE;
}





/*
BOOL SetPrivilege(HANDLE hToken,   LPCTSTR lpszPrivilege,BOOL bEnablePrivilege ){
	TOKEN_PRIVILEGES tp;
	LUID luid;
	if (!LookupPrivilegeValue(NULL, lpszPrivilege,&luid)){
		log_e(_T("LookupPrivilegeValue error: %u\n", GetLastError()));
		return FALSE;
	}
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	if (bEnablePrivilege)
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	else
		tp.Privileges[0].Attributes = 0;

	// Enable the privilege or disable all privileges.
	if (!AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),(PTOKEN_PRIVILEGES)NULL,(PDWORD)NULL)){
		log_e(_T("AdjustTokenPrivileges error: %u\n", GetLastError()));
		return FALSE;
	}
	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED){
		log_e(_T("The token does not have the specified privilege. \n"));
		return FALSE;
	}
	return TRUE;
}
*/


//�������
DogFood * CreateDogFood() {
	log_i(_T("���ɹ�����!\n"));

	SECURITY_ATTRIBUTES attributes;
	ZeroMemory(&attributes, sizeof(attributes));
	attributes.nLength = sizeof(attributes);

	ConvertStringSecurityDescriptorToSecurityDescriptor(
		L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GR;;;IU)",
		SDDL_REVISION_1,
		&attributes.lpSecurityDescriptor,
		NULL);

	HANDLE hMapFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,
		&attributes,
		PAGE_READWRITE,
		0,
		sizeof(DogFood),
		Memory_Name
	);

	int rst = GetLastError();
	if (rst) {
		log_e(_T("�ڴ�����ʧ��!%d\n"), rst);
		return NULL;
	}
	//��ȡ����
	DogFood * dogFood = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DogFood));
	return dogFood;
}

//����
void watching(DogFood * dogFood, wchar_t * commandLine) {
	//���Դ���
	int re_count = 0;
	//֮ǰ״̬
	char old_status = 0;
	long long old_timestamp = 0L;
	//��ʼ������
	dogFood->status = 0;
	dogFood->timestamp = 0L;
	log_i(_T("���Ź������ʼ�����,�ȴ�1���ӽ���ι������!\n"));
	//�ȴ�1����,��ʼ����ι������
	Sleep(1000 * 60);

	while (TRUE) {

		log_i(_T("������!\n"));
		//�������ʱ���Ϊ0 ���� ֹͣι������С��10,˵�������ʼ��ʧ����,����10��,ÿ�εȴ�ʱ���ӳ�һ����
		if (dogFood->timestamp == 0L && re_count < 10) {
			re_count++;
			log_e(_T("�����ʼ��ʧ��,��%d������!�ȴ�%d����...\n"), re_count, re_count + 1);
			TerminateProcess(pi.hProcess, 0);
			createProcess(commandLine);
			Sleep(1000 * 60 * (re_count + 1));
		}
		else if (dogFood->timestamp == 0L && re_count > 10) {
			//������Ȼ�ڳ�ʼ���Ĺ�����,���Ѿ�����10����,��ʱ���������Ϊ���޷�����
			log_e(_T("�����޷�����!\n"));
			TerminateProcess(pi.hProcess, 0);
			break;
		}
		else if (dogFood->timestamp == old_timestamp && dogFood->status == 1) {
			//������������,����û��ι��,��������
			log_e(_T("��ʱ��û��ι��,��������!\n"));
			re_count = 0;
			old_status = 0;
			old_timestamp = 0L;
			dogFood->status = 0;
			dogFood->timestamp = 0L;

			TerminateProcess(pi.hProcess, 0);
			createProcess(commandLine);
			Sleep(1000 * 60);

		}
		else {
			log_i(_T("ι���ɹ�!timestamp:%lld\n"),old_timestamp);
			old_status = dogFood->status;
			old_timestamp = dogFood->timestamp;
			Sleep(1000 * 15);
		}

	}
}