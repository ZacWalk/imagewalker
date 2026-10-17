#pragma once

namespace iw::imaging::detail
{
	class OperationStatus final : public IW::IStatus
	{
	public:
		OperationStatus(const DecodeOptions& options, OperationResult& result) : options_(options), result_(result) {}
		void SetMessage(const CString&) override {}
		void SetWarning(const CString& value) override { result_.warnings.emplace_back(value.GetString()); }
		void SetError(const CString& value) override
		{
			result_.message = value.GetString();
			if (result_.error == ImageError::none) result_.error = ImageError::read_failed;
		}
		void SetResourceError(const CString& value) override
		{
			result_.error = ImageError::resource_limit;
			result_.message = value.GetString();
		}
		void SetContext(const CString&) override {}
		void SetStatusMessage(const CString&) override {}
		void SetHighLevelStatusMessage(const CString&) override {}
		void Progress(int current, int total) override { if (options_.progress) options_.progress(current, total); }
		void SetHighLevelProgress(int current, int total) override { Progress(current, total); }
		bool QueryCancel() override
		{
			return options_.stop.stop_requested() || (options_.cancelled && options_.cancelled());
		}
	private:
		const DecodeOptions& options_;
		OperationResult& result_;
	};
}
